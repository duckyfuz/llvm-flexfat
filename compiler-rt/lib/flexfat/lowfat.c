//===-- lowfat.c - FlexFat runtime: encoding, init, allocator, reporter ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FlexFat runtime core — a focused port of the reference LowFat runtime.
//   Unit 3: pointer-encoding accessors (lowfat.h), the SIZES/MAGICS tables,
//           region reservation, constructor/preinit.
//   Unit 4: the per-size-class bump+freelist allocator (lowfat_malloc.c).
//   Unit 5: pointer classification, the memops (lowfat_memops.c), and the OOB
//           reporter. The reporter formatting (banner, "LOWFAT ERROR:" text,
//           the field layout, ANSI coloring) is a VERBATIM port — its uncolored
//           output is byte-identical to the reference, which the e2e CHECKs and
//           the MSET differential depend on.
//
// Exit convention: lowfat_oob_error -> lowfat_error -> abort() (SIGABRT / 6),
// matching the reference and the MSET lowfat_original/lowfat configs. See
// docs/STATUS.md for the exit-code decision.
//
//===----------------------------------------------------------------------===//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <execinfo.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/types.h>
#include <unistd.h>

#define LOWFAT_PAGE_SIZE   4096
#define LOWFAT_MAX_ADDRESS 0x1000000000000ull // 2^48
#define LOWFAT_CONSTRUCTOR __attribute__((__constructor__(10102)))
#define LOWFAT_NOINLINE    __attribute__((__noinline__))
#define LOWFAT_NORETURN    __attribute__((__noreturn__))
#define LOWFAT_CONST       __attribute__((__const__))
#define LOWFAT_DATA        /* EMPTY */
#define LOWFAT_ALIAS(name) __attribute__((__alias__(name)))
#define LOWFAT_CPUID(a, c, ax, bx, cx, dx)                                     \
  __asm__ __volatile__("cpuid"                                                 \
                       : "=a"(ax), "=b"(bx), "=c"(cx), "=d"(dx)                \
                       : "a"(a), "c"(c))

#define LOWFAT_SIZES  _LOWFAT_SIZES
#define LOWFAT_MAGICS _LOWFAT_MAGICS

#include "lowfat_config.c"
#include "lowfat.h"

static bool lowfat_malloc_inited = false;

//===----------------------------------------------------------------------===//
// Low-level helpers.
//===----------------------------------------------------------------------===//

// Per-region lock (the default reference build uses a pthread mutex).
typedef pthread_mutex_t lowfat_mutex_t;
static inline bool lowfat_mutex_init(lowfat_mutex_t *m) {
  return (pthread_mutex_init(m, NULL) == 0);
}
static inline void lowfat_mutex_lock(lowfat_mutex_t *m) { pthread_mutex_lock(m); }
static inline void lowfat_mutex_unlock(lowfat_mutex_t *m) {
  pthread_mutex_unlock(m);
}

static void lowfat_rand(void *buf, size_t len) {
  uint8_t *p = (uint8_t *)buf;
  while (len > 0) {
    ssize_t n = getrandom(p, len, 0);
    if (n <= 0) {
      memset(p, 0, len);
      return;
    }
    p += n;
    len -= (size_t)n;
  }
}

static void lowfat_dont_need(void *ptr, size_t size) {
  madvise(ptr, size, MADV_DONTNEED);
}

static void *lowfat_map(void *addr, size_t len, bool read, bool write) {
  int prot = (read ? PROT_READ : 0) | (write ? PROT_WRITE : 0);
  int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
  if (addr != NULL)
    flags |= MAP_FIXED_NOREPLACE;
  return mmap(addr, len, prot, flags, -1, 0);
}

static bool lowfat_protect(void *addr, size_t len, bool read, bool write) {
  int prot = (read ? PROT_READ : 0) | (write ? PROT_WRITE : 0);
  return (mprotect(addr, len, prot) == 0);
}

static LOWFAT_CONST void *lowfat_region(size_t idx) {
  return (void *)(idx * LOWFAT_REGION_SIZE);
}

// ANSI color only on a TTY (verbatim; keeps non-TTY output byte-identical).
static LOWFAT_NOINLINE const char *lowfat_color_escape_code(FILE *stream,
                                                            bool red) {
  int err = errno;
  int r = isatty(fileno(stream));
  errno = err;
  if (!r)
    return "";
  else
    return (red ? "\33[31m" : "\33[0m");
}

static LOWFAT_NOINLINE void lowfat_backtrace(void) {
  size_t MAX_TRACE = 256;
  void *trace[MAX_TRACE];
  int len = backtrace(trace, sizeof(trace) / sizeof(void *));
  char **trace_strs = backtrace_symbols(trace, len);
  for (int i = 0; i < len; i++)
    fprintf(stderr, "%d: %s\n", i, trace_strs[i]);
  if (len == 0 || len == (int)(sizeof(trace) / sizeof(void *)))
    fprintf(stderr, "...\n");
}

//===----------------------------------------------------------------------===//
// Pointer classification (verbatim; SPEC §5.4).
//===----------------------------------------------------------------------===//

LOWFAT_CONST bool lowfat_is_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  return (idx - 1) <= LOWFAT_NUM_REGIONS;
}

LOWFAT_CONST bool lowfat_is_stack_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  uintptr_t stack_end = (uintptr_t)lowfat_region(idx) +
                        LOWFAT_STACK_MEMORY_OFFSET + LOWFAT_STACK_MEMORY_SIZE;
  return lowfat_is_ptr(ptr) &&
         ((stack_end - (uintptr_t)ptr) <= LOWFAT_STACK_MEMORY_SIZE);
}

LOWFAT_CONST bool lowfat_is_global_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  uintptr_t global_end = (uintptr_t)lowfat_region(idx) +
                         LOWFAT_GLOBAL_MEMORY_OFFSET + LOWFAT_GLOBAL_MEMORY_SIZE;
  return lowfat_is_ptr(ptr) &&
         ((global_end - (uintptr_t)ptr) <= LOWFAT_GLOBAL_MEMORY_SIZE);
}

LOWFAT_CONST bool lowfat_is_heap_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  uintptr_t heap_end = (uintptr_t)lowfat_region(idx) + LOWFAT_HEAP_MEMORY_OFFSET +
                       LOWFAT_HEAP_MEMORY_SIZE;
  return lowfat_is_ptr(ptr) &&
         ((heap_end - (uintptr_t)ptr) <= LOWFAT_HEAP_MEMORY_SIZE);
}

//===----------------------------------------------------------------------===//
// Error / warning reporting (VERBATIM port — output is byte-identical).
//===----------------------------------------------------------------------===//

static size_t lowfat_num_messages = 0;
static pthread_mutex_t lowfat_print_mutex = PTHREAD_MUTEX_INITIALIZER;

static LOWFAT_NOINLINE void lowfat_print_banner(void) {
  fprintf(stderr,
          "%s"
          "_|                                      _|_|_|_|            _|\n"
          "_|          _|_|    _|      _|      _|  _|        _|_|_|  _|_|_|_|\n"
          "_|        _|    _|  _|      _|      _|  _|_|_|  _|    _|    _|\n"
          "_|        _|    _|    _|  _|  _|  _|    _|      _|    _|    _|\n"
          "_|_|_|_|    _|_|        _|      _|      _|        _|_|_|      _|_|%s\n"
          "\n",
          lowfat_color_escape_code(stderr, true),
          lowfat_color_escape_code(stderr, false));
}

static LOWFAT_NOINLINE void lowfat_message(const char *format, bool err,
                                           va_list ap) {
  lowfat_mutex_lock(&lowfat_print_mutex);

  // (1) Print the error:
  lowfat_print_banner();
  fprintf(stderr, "%sLOWFAT %s%s: ", lowfat_color_escape_code(stderr, true),
          (err ? "ERROR" : "WARNING"), lowfat_color_escape_code(stderr, false));
  vfprintf(stderr, format, ap);
  fputc('\n', stderr);

  // (2) Dump the stack:
  if (lowfat_malloc_inited)
    lowfat_backtrace();

  lowfat_num_messages++;
  lowfat_mutex_unlock(&lowfat_print_mutex);
}

LOWFAT_NOINLINE LOWFAT_NORETURN void lowfat_error(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  lowfat_message(format, /*err=*/true, ap);
  va_end(ap);
  abort();
}

LOWFAT_NOINLINE void lowfat_warning(const char *format, ...) {
  va_list ap;
  va_start(ap, format);
  lowfat_message(format, /*err=*/false, ap);
  va_end(ap);
}

size_t lowfat_get_num_errors(void) { return lowfat_num_messages; }

static LOWFAT_NOINLINE const char *lowfat_kind(const void *ptr) {
  if (!lowfat_is_ptr(ptr))
    return "nonfat";
  if (lowfat_is_heap_ptr(ptr))
    return "heap";
  if (lowfat_is_stack_ptr(ptr))
    return "stack";
  if (lowfat_is_global_ptr(ptr))
    return "global";
  return "unused";
}

static LOWFAT_NOINLINE const char *lowfat_error_kind(unsigned info) {
  switch (info) {
  case LOWFAT_OOB_ERROR_READ:
    return "read";
  case LOWFAT_OOB_ERROR_WRITE:
    return "write";
  case LOWFAT_OOB_ERROR_MEMCPY:
    return "memcpy";
  case LOWFAT_OOB_ERROR_MEMSET:
    return "memset";
  case LOWFAT_OOB_ERROR_ESCAPE_CALL:
    return "escape (call)";
  case LOWFAT_OOB_ERROR_ESCAPE_RETURN:
    return "escape (return)";
  case LOWFAT_OOB_ERROR_ESCAPE_STORE:
    return "escape (store)";
  case LOWFAT_OOB_ERROR_ESCAPE_PTR2INT:
    return "escape (ptr2int)";
  case LOWFAT_OOB_ERROR_ESCAPE_INSERT:
    return "escape (insert)";
  default:
    return "unknown";
  }
}

LOWFAT_NORETURN void lowfat_oob_error(unsigned info, const void *ptr,
                                      const void *baseptr) {
  const char *kind = lowfat_error_kind(info);
  ssize_t overflow = (ssize_t)ptr - (ssize_t)baseptr;
  if (overflow > 0)
    overflow -= lowfat_size(baseptr);
  lowfat_error("out-of-bounds error detected!\n"
               "\toperation = %s\n"
               "\tpointer   = %p (%s)\n"
               "\tbase      = %p\n"
               "\tsize      = %zu\n"
               "\t%s = %+zd\n",
               kind, ptr, lowfat_kind(ptr), baseptr, lowfat_size(baseptr),
               (overflow < 0 ? "underflow" : "overflow "), overflow);
}

void lowfat_oob_warning(unsigned info, const void *ptr, const void *baseptr) {
  const char *kind = lowfat_error_kind(info);
  ssize_t overflow = (ssize_t)ptr - (ssize_t)baseptr;
  if (overflow > 0)
    overflow -= lowfat_size(baseptr);
  lowfat_warning("out-of-bounds error detected!\n"
                 "\toperation = %s\n"
                 "\tpointer   = %p (%s)\n"
                 "\tbase      = %p\n"
                 "\tsize      = %zu\n"
                 "\t%s = %+zd\n",
                 kind, ptr, lowfat_kind(ptr), baseptr, lowfat_size(baseptr),
                 (overflow < 0 ? "underflow" : "overflow "), overflow);
}

void lowfat_oob_check(unsigned info, const void *ptr, size_t size0,
                      const void *baseptr) {
  size_t size = lowfat_size(baseptr);
  size_t diff = (size_t)((const uint8_t *)ptr - (const uint8_t *)baseptr);
  size -= size0;
  if (diff >= size)
    lowfat_oob_error(info, ptr, baseptr);
}

//===----------------------------------------------------------------------===//
// The allocator and bounds-checked memops (#included as part of this TU).
//===----------------------------------------------------------------------===//

#include "lowfat_malloc.c"
#include "lowfat_memops.c"

//===----------------------------------------------------------------------===//
// Init: build the tables, reserve the regions, initialise the allocator.
//===----------------------------------------------------------------------===//

static LOWFAT_NOINLINE LOWFAT_NORETURN void lowfat_init_error(const char *msg) {
  fprintf(stderr, "FlexFat runtime init error: %s: %s\n", msg, strerror(errno));
  abort();
}

static bool lowfat_inited = false;

void LOWFAT_CONSTRUCTOR lowfat_init(void) {
  if (lowfat_inited)
    return;
  lowfat_inited = true;

  // Sanity checks.
  if (sizeof(void *) != sizeof(uint64_t))
    lowfat_init_error("incompatible architecture (not x86-64)");
  if (sysconf(_SC_PAGESIZE) != LOWFAT_PAGE_SIZE)
    lowfat_init_error("incompatible system page size");
#if !defined(LOWFAT_LEGACY)
  {
    uint32_t eax, ebx, ecx, edx;
    LOWFAT_CPUID(7, 0, eax, ebx, ecx, edx);
    if (((ebx >> 3) & 1) == 0 || ((ebx >> 8) & 1) == 0)
      lowfat_init_error("incompatible architecture (no BMI/BMI2)");
  }
#endif

  // SIZES/MAGICS cover the full index range:
  //   total_pages = (2^48 / REGION_SIZE) / (PAGE / sizeof(size_t)).
  size_t total_pages = (LOWFAT_MAX_ADDRESS / LOWFAT_REGION_SIZE) /
                       (LOWFAT_PAGE_SIZE / sizeof(size_t));
  size_t len = total_pages * LOWFAT_PAGE_SIZE;
  size_t entries = len / sizeof(size_t);

  size_t *sizes = (size_t *)lowfat_map((void *)LOWFAT_SIZES, len, true, true);
  if (sizes != (size_t *)LOWFAT_SIZES)
    lowfat_init_error("failed to mmap SIZES table");
  uint64_t *magics =
      (uint64_t *)lowfat_map((void *)LOWFAT_MAGICS, len, true, true);
  if (magics != (uint64_t *)LOWFAT_MAGICS)
    lowfat_init_error("failed to mmap MAGICS table");

  for (size_t i = 0; i < entries; i++) {
    sizes[i] = SIZE_MAX;
    magics[i] = 0;
  }
  size_t sizes_len = sizeof(lowfat_sizes) / sizeof(lowfat_sizes[0]);
  for (size_t j = 0; j < sizes_len; j++) {
    sizes[1 + j] = lowfat_sizes[j];
    magics[1 + j] = lowfat_magics[j];
  }

  if (!lowfat_protect((void *)LOWFAT_SIZES, len, true, false) ||
      !lowfat_protect((void *)LOWFAT_MAGICS, len, true, false))
    lowfat_init_error("failed to write-protect tables");

  // Reserve each size-class region (PROT_NONE, MAP_NORESERVE).
  for (size_t i = 1; i <= LOWFAT_NUM_REGIONS; i++) {
    void *heap_start = (uint8_t *)lowfat_region(i) + LOWFAT_HEAP_MEMORY_OFFSET;
    void *ptr = lowfat_map(heap_start, LOWFAT_HEAP_MEMORY_SIZE, false, false);
    if (ptr != heap_start)
      lowfat_init_error("failed to reserve region");
  }

  // Initialise the allocator (ASLR start + lazy-commit bookkeeping per region).
  if (!lowfat_malloc_init())
    lowfat_init_error("failed to initialise allocator");
  lowfat_malloc_inited = true;
}

// Run before ordinary constructors (matches the reference .preinit_array entry).
static void lowfat_preinit(int argc, char **argv, char **envp) {
  (void)argc;
  (void)argv;
  (void)envp;
  lowfat_init();
}
__attribute__((used, section(".preinit_array"))) static void (
    *lowfat_preinit_ptr)(int, char **, char **) = lowfat_preinit;
