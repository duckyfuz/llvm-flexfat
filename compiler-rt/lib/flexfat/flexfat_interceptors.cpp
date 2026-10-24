//===-- flexfat_interceptors.cpp - FlexFat Malloc/Free Interceptors
//-------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "flexfat_allocator.h"
#include "flexfat_config.h"
#include "flexfat_interface.h"
#include "flexfat_stack.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_checks.h"
#include "sanitizer_common/sanitizer_allocator_dlsym.h"
#include "sanitizer_common/sanitizer_allocator_report.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_errno_codes.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"

using namespace __sanitizer;

DECLARE_REAL(void *, malloc, uptr size)

// DlsymAlloc handles allocations that happen before our runtime is initialized
// (e.g., during dynamic linker symbol resolution). Uses a small static buffer.
namespace {
struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !__flexfat::IsReady(); }
};
} // namespace

// Helper: should this allocation go through FlexFat?
// Allocations larger than our max size class fall back to system malloc.
static inline bool ShouldUseFlexFat(uptr size) {
  return __flexfat::IsReady() && size < __flexfat::kMaxSize;
}

static inline void check_bounds(const void *ptr, uptr access_size,
                                int is_write);

static inline void *ManagedOrSystemMalloc(uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  if (ShouldUseFlexFat(size))
    if (void *ptr = __flexfat::Allocate(size))
      return ptr;
  return REAL(malloc)(size);
}

static inline bool IsValidPowerOfTwo(uptr value) {
  return value && (value & (value - 1)) == 0;
}

static inline bool CanManageAligned(uptr alignment) {
  // Let LowFat's aligned-allocation policy select the class, including the
  // alignment-sized-slot fast path when size < alignment.
  return __flexfat::IsReady() && IsValidPowerOfTwo(alignment);
}

static inline void *ManagedOrSystemAligned(uptr alignment, uptr size,
                                           void *(*fallback)(uptr, uptr)) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size, alignment);
  if (CanManageAligned(alignment))
    if (void *ptr = __flexfat::AllocateAligned(size, alignment))
      return ptr;
  return fallback(alignment, size);
}

//===----------------------------------------------------------------------===//
// Interceptors
//===----------------------------------------------------------------------===//

INTERCEPTOR(void *, malloc, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  return ManagedOrSystemMalloc(size);
}

INTERCEPTOR(void, free, void *ptr) {
  if (!ptr)
    return;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  if (__flexfat::IsFlexFatPointer((uptr)ptr)) {
    __flexfat::Deallocate(ptr);
    return;
  }
  REAL(free)(ptr);
}

INTERCEPTOR(void *, calloc, uptr nmemb, uptr size) {
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  if (UNLIKELY(CheckForCallocOverflow(nmemb, size))) {
    if (AllocatorMayReturnNull())
      return SetErrnoOnNull(nullptr);
    GET_STACK_TRACE_FATAL_HERE;
    ReportCallocOverflow(nmemb, size, &stack);
  }
  uptr total = nmemb * size;
  if (ShouldUseFlexFat(total)) {
    void *ptr = __flexfat::Allocate(total);
    if (ptr) {
      internal_memset(ptr, 0, total);
      return ptr;
    }
  }
  return REAL(calloc)(nmemb, size);
}

INTERCEPTOR(void *, realloc, void *ptr, uptr size) {
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);

  // realloc(nullptr, size) == malloc(size)
  if (!ptr) {
    return ManagedOrSystemMalloc(size);
  }

#ifdef FLEXFAT_TEMPORAL_TBI
  __flexfat_check_temporal((uptr)ptr, 0, 3);
#endif

  // Preserve LowFat's zero-size policy: allocate a new minimum-sized object
  // and leave the old object untouched.
  if (size == 0)
    return ManagedOrSystemMalloc(size);

  if (!__flexfat::IsFlexFatPointer((uptr)ptr))
    return REAL(realloc)(ptr, size);

  uptr old_class_size = __flexfat::GetSize((uptr)ptr);
  uptr old_base = __flexfat::GetBase((uptr)ptr);
  uptr old_offset = __flexfat::Untag((uptr)ptr) - old_base;
  uptr index = __flexfat::AllocationClass(size);
  if (index == __flexfat::GetRegionIndex((uptr)ptr) &&
      size <= __flexfat::UsableClassSize(old_class_size) - old_offset) {
    // lowfat_realloc() keeps allocations in the same class in place and
    // makes the complete slot accessible, including any former guard pages.
    __flexfat::MakeAllocationAccessible((uptr)ptr);
    return ptr;
  }

  void *new_ptr = ManagedOrSystemMalloc(size);
  if (!new_ptr)
    return nullptr;
  // LowFat tracks the slot size rather than the original request. Unprotect
  // it before copying, since large allocations may contain inaccessible pages.
  __flexfat::MakeAllocationAccessible((uptr)ptr);
  uptr old_usable = __flexfat::UsableClassSize(old_class_size) - old_offset;
  internal_memcpy(new_ptr, ptr, Min(size, old_usable));
  __flexfat::Deallocate(ptr);
  return new_ptr;
}

INTERCEPTOR(void *, valloc, uptr size) {
  uptr alignment = GetPageSizeCached();
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size, alignment);
  if (CanManageAligned(alignment))
    if (void *ptr = __flexfat::AllocateAligned(size, alignment))
      return ptr;
  return REAL(valloc)(size);
}

INTERCEPTOR(int, posix_memalign, void **memptr, uptr alignment, uptr size) {
  if (!IsValidPowerOfTwo(alignment) || alignment % sizeof(void *) != 0)
    return errno_EINVAL;
  if (DlsymAlloc::Use()) {
    *memptr = DlsymAlloc::Allocate(size, alignment);
    return 0;
  }
#ifdef FLEXFAT_TEMPORAL_TBI
  __flexfat_check_temporal((uptr)memptr, sizeof(*memptr), 1);
#endif
  if (CanManageAligned(alignment)) {
    if (void *ptr = __flexfat::AllocateAligned(size, alignment)) {
      *memptr = ptr;
      return 0;
    }
  }
  return REAL(posix_memalign)(memptr, alignment, size);
}

#if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void *, memalign, uptr alignment, uptr size) {
  if (!IsValidPowerOfTwo(alignment)) {
    errno = errno_EINVAL;
    return nullptr;
  }
  return ManagedOrSystemAligned(alignment, size, REAL(memalign));
}
#endif

#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR(void *, aligned_alloc, uptr alignment, uptr size) {
  if (!IsValidPowerOfTwo(alignment) || size % alignment != 0) {
    errno = errno_EINVAL;
    return nullptr;
  }
  return ManagedOrSystemAligned(alignment, size, REAL(aligned_alloc));
}
#endif

#if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR(void *, pvalloc, uptr size) {
  uptr page = GetPageSizeCached();
  if (size > ~(uptr)0 - (page - 1)) {
    errno = errno_ENOMEM;
    return nullptr;
  }
  uptr rounded = RoundUpTo(size ? size : 1, page);
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(rounded, page);
  if (CanManageAligned(page))
    if (void *ptr = __flexfat::AllocateAligned(rounded, page))
      return ptr;
  return REAL(pvalloc)(size);
}
#endif

static uptr BoundedStringLength(const char *src, uptr limit) {
  return internal_strnlen(src, limit);
}

static char *DuplicateString(const char *src, uptr requested_limit,
                             bool bounded) {
#ifdef FLEXFAT_TEMPORAL_TBI
  if (!bounded || requested_limit)
    __flexfat_check_temporal((uptr)src, 1, 0);
#endif
  uptr scan_limit = requested_limit;
  bool managed = __flexfat::IsFlexFatPointer((uptr)src);
  if (managed) {
    uptr base = __flexfat::GetBase((uptr)src);
    uptr available = __flexfat::UsableClassSize(__flexfat::GetSize((uptr)src)) -
                     (__flexfat::Untag((uptr)src) - base);
    if (!bounded || scan_limit > available)
      scan_limit = available;
  }
  uptr length = bounded ? BoundedStringLength(src, scan_limit)
                        : (managed ? BoundedStringLength(src, scan_limit)
                                   : internal_strlen(src));
  if (managed && length == scan_limit &&
      (!bounded || requested_limit > scan_limit)) {
    check_bounds(src, scan_limit + 1, 0);
    return nullptr;
  }
  if (length == ~(uptr)0)
    return (char *)SetErrnoOnNull(nullptr);
  char *result = (char *)ManagedOrSystemMalloc(length + 1);
  if (!result)
    return nullptr;
  internal_memcpy(result, src, length);
  result[length] = '\0';
  return result;
}

INTERCEPTOR(char *, strdup, const char *src) {
  return DuplicateString(src, 0, false);
}

#if SANITIZER_INTERCEPT_STRNDUP
INTERCEPTOR(char *, strndup, const char *src, uptr size) {
  return DuplicateString(src, size, true);
}
#endif

static inline void check_bounds(const void *ptr, uptr access_size,
                                int is_write) {
  if (!__flexfat::IsReady() || !ptr || access_size == 0)
    return;
#ifdef FLEXFAT_TEMPORAL_TBI
  __flexfat_check_temporal((uptr)ptr, access_size, is_write);
#endif
  if (!__flexfat::CheckBounds((uptr)ptr, access_size)) {
    uptr start = __flexfat::Untag((uptr)ptr);
    uptr size = __flexfat::GetSize(start);
    uptr bound = __flexfat::UsableClassSize(size);
    uptr base = __flexfat::GetBase(start);
    uptr report_ptr;
    if (access_size <= ~(uptr)0 - start) {
      report_ptr = start + access_size;
    } else {
      // The end of the access cannot be represented.  Report just beyond the
      // allocation instead of UINTPTR_MAX, which is printed as -1 by the
      // signed overflow diagnostic.
      if (bound <= ~(uptr)0 - base) {
        report_ptr = base + bound;
        if (report_ptr != ~(uptr)0)
          ++report_ptr;
      } else {
        // This configuration cannot represent the allocation end either.
        // The access start is still the most useful representable location.
        report_ptr = start;
      }
    }
    __flexfat_report_oob(report_ptr, base, bound, is_write);
  }
}

// The compiler pass instruments direct memory accesses inline, but cannot
// instrument external libc calls. We intercept them here to check bounds.
INTERCEPTOR(void *, memset, void *dst, int v, uptr size) {
  check_bounds(dst, size, 1 /* write */);
  if (!__flexfat::IsReady())
    return internal_memset(dst, v, size);
  return REAL(memset)(dst, v, size);
}

INTERCEPTOR(void *, memcpy, void *dst, const void *src, uptr size) {
  check_bounds(dst, size, 1 /* write */);
  check_bounds(src, size, 0 /* read */);
  if (!__flexfat::IsReady())
    return internal_memcpy(dst, src, size);
  return REAL(memcpy)(dst, src, size);
}

INTERCEPTOR(void *, memmove, void *dst, const void *src, uptr size) {
  check_bounds(dst, size, 1 /* write */);
  check_bounds(src, size, 0 /* read */);
  if (!__flexfat::IsReady())
    return internal_memmove(dst, src, size);
  return REAL(memmove)(dst, src, size);
}

#if SANITIZER_INTERCEPT_MALLOC_USABLE_SIZE
INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  if (!ptr)
    return 0;
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::GetSize(ptr);
  if (__flexfat::IsFlexFatPointer((uptr)ptr))
    return __flexfat_get_usable_size((uptr)ptr);
  return REAL(malloc_usable_size)(ptr);
}
#endif

#if SANITIZER_APPLE
INTERCEPTOR(uptr, malloc_size, void *ptr) {
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::GetSize(ptr);
  if (__flexfat::IsFlexFatPointer((uptr)ptr))
    return __flexfat::GetSize((uptr)ptr);
  return REAL(malloc_size)(ptr);
}
#endif

namespace __flexfat {
void InitializeInterceptors() {
  static int inited = 0;
  CHECK_EQ(inited, 0);

  INTERCEPT_FUNCTION(malloc);
  INTERCEPT_FUNCTION(free);
  INTERCEPT_FUNCTION(calloc);
  INTERCEPT_FUNCTION(realloc);
  INTERCEPT_FUNCTION(valloc);
  INTERCEPT_FUNCTION(posix_memalign);
#if SANITIZER_INTERCEPT_MEMALIGN
  INTERCEPT_FUNCTION(memalign);
#endif
#if SANITIZER_INTERCEPT_ALIGNED_ALLOC
  INTERCEPT_FUNCTION(aligned_alloc);
#endif
#if SANITIZER_INTERCEPT_PVALLOC
  INTERCEPT_FUNCTION(pvalloc);
#endif
  INTERCEPT_FUNCTION(strdup);
#if SANITIZER_INTERCEPT_STRNDUP
  INTERCEPT_FUNCTION(strndup);
#endif
  INTERCEPT_FUNCTION(memset);
  INTERCEPT_FUNCTION(memcpy);
  INTERCEPT_FUNCTION(memmove);
#if SANITIZER_INTERCEPT_MALLOC_USABLE_SIZE
  INTERCEPT_FUNCTION(malloc_usable_size);
#endif
#if SANITIZER_APPLE
  INTERCEPT_FUNCTION(malloc_size);
#endif
  inited = 1;
}
} // namespace __flexfat
