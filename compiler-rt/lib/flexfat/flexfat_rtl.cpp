//===-- flexfat_rtl.cpp - FlexFat Sanitizer Runtime Library
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is the main file of the FlexFat Sanitizer runtime library.
//
// FlexFat pointers encode allocation bounds information directly in the pointer
// value through careful memory layout. This allows O(1) bounds checking without
// maintaining separate metadata.
//
//===----------------------------------------------------------------------===//

#include "flexfat_allocator.h"
#include "flexfat_config.h"
#include "flexfat_interface.h"
#include "flexfat_stack.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include <stddef.h>

using namespace __sanitizer;

namespace __flexfat {

// Flag to track initialization state (not static — accessed by
// flexfat_interceptors.cpp)
bool flexfat_inited = false;

// Set to true when -fsanitize-recover=flexfat is active. Controls whether
// interceptor-level OOB (memset/memcpy/memmove) warns-and-continues or aborts.
bool flexfat_recover = false;

// Set to true when -flexfat-mode=right-align is active. Instructs Allocate()
// to bias objects toward the high end of their size-class slot while still
// preserving the default malloc alignment guarantee. This can improve detection
// of some right-side overflows, but the object's right edge does not always
// coincide exactly with the slot boundary once alignment is enforced.
bool flexfat_right_align = false;

// malloc() must return a pointer suitably aligned for any object type.
static constexpr uptr kMallocAlignment = alignof(max_align_t);

// Maximum number of size classes across both modes.
// In POW2-only mode kNumSizeClasses=27; with a custom config it can be up to
// FLEXFAT_MAX_ARRAY_SIZE. We size the static arrays at compile time using
// whichever is larger so the same translation unit works in both modes.
#ifdef FLEXFAT_CUSTOM_CONFIG
static constexpr uptr kMaxSizeClasses = FLEXFAT_NUM_SIZE_CLASSES;
#else
static constexpr uptr kMaxSizeClasses = kNumSizeClasses;
#endif

// Region table - initialized in __flexfat_init
RegionInfo kRegions[kMaxSizeClasses];

// Pointers to the start of each mapped region
static uptr region_bases[kMaxSizeClasses];

// Bump pointer: next fresh address to allocate from in each region
static uptr region_next_alloc[kMaxSizeClasses];

// Segregated free lists: one singly-linked list per size class
// Free blocks store a pointer to the next free block at their start
struct FreeBlock {
  FreeBlock *next;
};
static FreeBlock *free_lists[kMaxSizeClasses];

// Per-size-class spin mutexes protecting region_next_alloc and free_lists.
// Using one lock per size class allows concurrent allocation across different
// size classes, which is the common case in multi-threaded programs.
static StaticSpinMutex region_locks[kMaxSizeClasses];

// Fixed address where the metadata tables are mapped during initialization.
// Pow2 mode uses the size and mask tables. Custom mode uses the size and magic
// tables. This allows the LLVM pass to use absolute addressing (imm[index*8])
// instead of PC-relative loads.
//
//   kTablesBase + 0 * kTablesOffset: Sizes (8 bytes per class)
//   kTablesBase + 1 * kTablesOffset: Magics (custom mode)
//   kTablesBase + 3 * kTablesOffset: Masks (POW2 mode)
// Custom mode generates kTablesBase together with its region geometry.
static constexpr uptr kTablesOffset = 0x1000000ULL; // 16 MB between tables

static void InitializeFlags() {
  SetCommonFlagsDefaults();

  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.exitcode = 1;           // Fatal OOB exits with code 1 by default
    cf.abort_on_error = false; // Use the exitcode path, not SIGABRT, so output
                               // is flushed before the process exits
    OverrideCommonFlags(cf);
  }

  // Register all common flags with a parser and read FLEXFAT_OPTIONS.
  //    Allow overriding flags at runtime, e.g.:
  //    FLEXFAT_OPTIONS=exitcode=42:verbosity=1 ./my_program
  FlagParser parser;
  RegisterCommonFlags(&parser);
  parser.ParseStringFromEnv("FLEXFAT_OPTIONS");

  InitializeCommonFlags();
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
}

static void InitTables() {
  // Reserve enough address space for the fixed table offsets used by the pass.
  if (!MmapFixedNoReserve(kTablesBase, 64 * 1024 * 1024, "flexfat_tables"))
    Die();

  u64 *sizes = (u64 *)(kTablesBase + 0 * kTablesOffset);
#ifdef FLEXFAT_CUSTOM_CONFIG
  u64 *magics = (u64 *)(kTablesBase + 1 * kTablesOffset);
#else
  u64 *masks = (u64 *)(kTablesBase + 3 * kTablesOffset);
#endif

  // Initialize all possible region indices (up to 1024 for now, which covers
  // 32TB) with "poison" values: Size=0, Base=0. Any access to a non-FlexFat
  // pointer will then result in (Base=0, End=0), which always fails the OOB
  // check:
  //   Ptr < 0 || Ptr >= 0  => Always True.
  for (uptr i = 0; i < 1024; i++) {
    if (i < kNumSizeClasses) {
#ifdef FLEXFAT_CUSTOM_CONFIG
      sizes[i] = (u64)kFlexFatGenSizes[i];
      magics[i] = (u64)kFlexFatGenMagics[i];
#else
      u64 size = (u64)SizeClassToSize(i);
      sizes[i] = size;
      masks[i] = ~(size - 1);
#endif
    } else {
      sizes[i] = 0;
#ifdef FLEXFAT_CUSTOM_CONFIG
      magics[i] = 0;
#else
      masks[i] = 0;
#endif
    }
  }
}

static void InitRegionTable() {
  for (uptr i = 0; i < kNumSizeClasses; i++) {
    uptr size = SizeClassToSize(i);
    kRegions[i].size = size;
    kRegions[i].alignment = size;
    free_lists[i] = nullptr;
  }
}

// Initialize memory regions using mmap
// Each region is mapped at a fixed address for the corresponding size class
static bool InitMemoryRegions() {
  for (uptr i = 0; i < kNumSizeClasses; i++) {
    uptr region_start = GetRegionStart(i);

    // Reserve the region without committing physical memory
    // MmapFixedNoReserve maps memory but doesn't allocate physical pages until
    // they're accessed
    bool success =
        MmapFixedNoReserve(region_start, kRegionSize, "flexfat_region");

    if (!success)
      return false;

    region_bases[i] = region_start;

    // The first allocation must be aligned to the object size relative to
    // absolute zero. This is required for the magic-number fixed point math to
    // securely compute object bases.
    uptr size = kRegions[i].size;
    uptr offset = region_start % size;
    uptr initial_alloc = region_start;
    if (offset != 0)
      initial_alloc += (size - offset);

    region_next_alloc[i] = initial_alloc;
  }

  return true;
}

// Allocate from a FlexFat region
// First checks the free list, then falls back to bump allocation.
// Thread-safe: protected by per-size-class spin mutex.
//
// In right-align mode, returns the highest malloc-aligned address within the
// slot that still leaves room for the requested object. The bounds check
// (ptr - GetBase(ptr)) < class_size is still correct: GetBase() recovers
// slot_base via reciprocal multiplication since slot_base is always
// class-aligned, and any
// access past slot_base+class_size fails the check.
//
// The free list always stores slot bases (not right-aligned pointers) so that
// freed blocks can be reused with a different offset for a new request size.
static void *AllocateImpl(uptr size, uptr alignment) {
  if (size == 0)
    size = 1;

  uptr class_request = size;
  if (alignment) {
    if (alignment - 1 > ~(uptr)0 - class_request)
      return nullptr;
    class_request += alignment - 1;
  }

  uptr class_index = SizeClassIndex(class_request);
  if (class_index >= kNumSizeClasses)
    return nullptr;

  uptr alloc_size = SizeClassToSize(class_index);

  SpinMutexLock lock(&region_locks[class_index]);

  uptr slot_base;

  // 1. Try free list first (stores slot bases)
  FreeBlock *block = free_lists[class_index];
  if (block) {
    free_lists[class_index] = block->next;
    slot_base = (uptr)block;
    // Zero the entire slot (free list pointer was stored at slot_base)
    internal_memset(block, 0, alloc_size);
  } else {
    // 2. Fall back to bump allocation
    uptr region_end = GetRegionStart(class_index) + kRegionSize;
    uptr addr = region_next_alloc[class_index];

    if (addr + alloc_size > region_end) {
      // Interceptors perform matched system-allocation fallback.  Returning an
      // InternalAlloc pointer here would later send it to libc free().
      return nullptr;
    }

    region_next_alloc[class_index] = addr + alloc_size;
    slot_base = addr;
  }

  // In right-align mode, preserve malloc alignment by rounding the available
  // slack down to the nearest alignment boundary before shifting the pointer.
  if (alignment) {
    return (void *)RoundUpTo(slot_base, alignment);
  }
  if (flexfat_right_align) {
    uptr slack = alloc_size - size;
    uptr aligned_offset = RoundDownTo(slack, kMallocAlignment);
    return (void *)(slot_base + aligned_offset);
  }
  return (void *)slot_base;
}

void *Allocate(uptr size) { return AllocateImpl(size, 0); }

void *AllocateAligned(uptr size, uptr alignment) {
  CHECK(IsPowerOfTwo(alignment));
  return AllocateImpl(size, alignment);
}

// Free a FlexFat allocation by pushing its slot base onto the free list.
// Thread-safe: protected by per-size-class spin mutex.
//
// We always push the slot base (GetBase(ptr)) rather than ptr itself so that
// freed slots can be reused with a different right-align offset for a new
// request size, and so the free list is consistent regardless of mode.
void Deallocate(void *ptr) {
  if (!ptr)
    return;

  uptr addr = (uptr)ptr;

  CHECK(IsFlexFatPointer(addr));

  uptr region = GetRegionIndex(addr);
  // Recover the slot base: in right-align mode ptr is offset within the slot;
  // in normal mode GetBase(addr) == addr since allocations are class-aligned.
  uptr slot_base = GetBase(addr);

  SpinMutexLock lock(&region_locks[region]);

  // Push slot base to the head of the free list for this size class
  FreeBlock *block = (FreeBlock *)slot_base;
  block->next = free_lists[region];
  free_lists[region] = block;
}

static void PrintOobHeader(const char *level, uptr ptr, uptr base, uptr bound,
                           int is_write) {
  // Compute the signed overflow: how many bytes past the end of the allocation
  // the access reached. 0 means exactly at the boundary.
  sptr overflow = (sptr)(ptr) - (sptr)(base + bound);
  const char *op = is_write ? "write" : "read";

  Printf("FLEXFAT %s: out-of-bounds error detected!\n", level);
  Printf("          operation = %s\n", op);
  Printf("          pointer   = 0x%zx (heap)\n", ptr);
  Printf("          base      = 0x%zx\n", base);
  Printf("          size      = %zu\n", bound);
  const char *sign = (overflow >= 0) ? "+" : "";
  Printf("          overflow  = %s%zd\n", sign, (long)overflow);
  Printf("\n");
}

static void PrintErrorAndDie(uptr ptr, uptr base, uptr bound, int is_write,
                             const StackTrace &stack) {
  PrintOobHeader("ERROR", ptr, base, bound, is_write);
  stack.Print();
  Die();
}

static void PrintWarning(uptr ptr, uptr base, uptr bound, int is_write,
                         const StackTrace &stack) {
  PrintOobHeader("WARNING", ptr, base, bound, is_write);
  stack.Print();
}

} // namespace __flexfat

// ---------------------- Interface Functions ----------------------

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_set_recover(int recover) {
  __flexfat::flexfat_recover = (recover != 0);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_set_right_align(int right_align) {
  __flexfat::flexfat_right_align = (right_align != 0);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_init() {
  if (__flexfat::flexfat_inited)
    return;

  __flexfat::InitializeFlags();

  __flexfat::InitTables();

  __flexfat::InitRegionTable();

  if (!__flexfat::InitMemoryRegions())
    Die();

  __flexfat::flexfat_inited = true;

  __flexfat::InitializeInterceptors();
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_report_oob(uptr ptr, uptr base, uptr bound, int is_write) {
  GET_STACK_TRACE_FATAL_HERE;
  __flexfat::PrintErrorAndDie(ptr, base, bound, is_write, stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_warn_oob(uptr ptr, uptr base, uptr bound, int is_write) {
  GET_STACK_TRACE_FATAL_HERE;
  __flexfat::PrintWarning(ptr, base, bound, is_write, stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_base(uptr ptr) { return __flexfat::GetBase(ptr); }

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_size(uptr ptr) { return __flexfat::GetSize(ptr); }

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_offset(uptr ptr) {
  uptr base = __flexfat::GetBase(ptr);
  if (base == 0)
    return 0;
  return ptr - base;
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_usable_size(uptr ptr) {
  uptr base = __flexfat::GetBase(ptr);
  uptr size = __flexfat::GetSize(ptr);
  if (base == 0)
    return (uptr)-1;
  return size - (ptr - base);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__flexfat_malloc(uptr size) { return __flexfat::Allocate(size); }

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_free(void *ptr) { __flexfat::Deallocate(ptr); }

} // extern "C"

#if SANITIZER_CAN_USE_PREINIT_ARRAY
// ELF platforms: use .preinit_array for earliest possible initialization
__attribute__((section(".preinit_array"), used)) static auto preinit =
    __flexfat_init;
#else
// macOS/other platforms: use constructor attribute
__attribute__((constructor)) static void flexfat_constructor() {
  __flexfat_init();
}
#endif
