//===-- lowfat.c - FlexFat runtime: pointer-encoding core + init ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FlexFat runtime core (Unit 3) — a focused port of the reference LowFat runtime
// (lowfat.h accessors + lowfat.c init). Implements only the pointer-encoding
// core: the SIZES/MAGICS tables at 0x200000/0x300000 (sized to cover the full
// 2^48 index range, mprotect-ed read-only), region reservation (PROT_NONE,
// MAP_NORESERVE), and constructor(10102) + .preinit_array ordering. Index 0 is
// the non-fat region (SIZE_MAX / 0). The allocator, stacks, threads, the SEGV
// handler and the stack pivot land in later units.
//
//===----------------------------------------------------------------------===//

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>

#define LOWFAT_PAGE_SIZE   4096
#define LOWFAT_MAX_ADDRESS 0x1000000000000ull // 2^48
#define LOWFAT_CONSTRUCTOR __attribute__((__constructor__(10102)))
#define LOWFAT_NOINLINE    __attribute__((__noinline__))
#define LOWFAT_NORETURN    __attribute__((__noreturn__))
#define LOWFAT_CONST       __attribute__((__const__))
#define LOWFAT_CPUID(a, c, ax, bx, cx, dx)                                     \
  __asm__ __volatile__("cpuid"                                                 \
                       : "=a"(ax), "=b"(bx), "=c"(cx), "=d"(dx)                \
                       : "a"(a), "c"(c))

#define LOWFAT_SIZES  _LOWFAT_SIZES
#define LOWFAT_MAGICS _LOWFAT_MAGICS

#include "lowfat_config.c"
#include "lowfat.h"

static LOWFAT_NOINLINE LOWFAT_NORETURN void lowfat_init_error(const char *msg) {
  fprintf(stderr, "FlexFat runtime init error: %s: %s\n", msg, strerror(errno));
  abort();
}

// Simplified mmap/mprotect wrappers (cf. the reference lowfat_linux.c). Uses
// MAP_FIXED_NOREPLACE rather than the reference's MAP_FIXED so a stray existing
// mapping is detected (the caller checks the returned address) rather than
// silently clobbered.
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

// Pointer classification (cf. reference lowfat.c §5.4 / lowfat-ptr-info.c).
bool lowfat_is_ptr(const void *ptr) {
  size_t idx = lowfat_index(ptr);
  return (idx != 0 && idx <= LOWFAT_NUM_REGIONS + 1);
}

bool lowfat_is_heap_ptr(const void *ptr) {
  if (!lowfat_is_ptr(ptr))
    return false;
  const uint8_t *lo =
      (const uint8_t *)lowfat_region(lowfat_index(ptr)) + LOWFAT_HEAP_MEMORY_OFFSET;
  const uint8_t *hi = lo + LOWFAT_HEAP_MEMORY_SIZE;
  return ((const uint8_t *)ptr >= lo && (const uint8_t *)ptr < hi);
}

bool lowfat_is_stack_ptr(const void *ptr) {
  if (!lowfat_is_ptr(ptr))
    return false;
  const uint8_t *lo = (const uint8_t *)lowfat_region(lowfat_index(ptr)) +
                      LOWFAT_STACK_MEMORY_OFFSET;
  const uint8_t *hi = lo + LOWFAT_STACK_MEMORY_SIZE;
  return ((const uint8_t *)ptr >= lo && (const uint8_t *)ptr < hi);
}

bool lowfat_is_global_ptr(const void *ptr) {
  if (!lowfat_is_ptr(ptr))
    return false;
  const uint8_t *lo = (const uint8_t *)lowfat_region(lowfat_index(ptr)) +
                      LOWFAT_GLOBAL_MEMORY_OFFSET;
  const uint8_t *hi = lo + LOWFAT_GLOBAL_MEMORY_SIZE;
  return ((const uint8_t *)ptr >= lo && (const uint8_t *)ptr < hi);
}

// Build the SIZES/MAGICS tables and reserve the size-class regions.
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

  // Index 0 is the non-fat region; every index outside [1, NUM_REGIONS] also
  // decodes to size=SIZE_MAX / magic=0 ("not low-fat -> never checked").
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

  // Reserve each size-class region (PROT_NONE, MAP_NORESERVE). Physical pages
  // are committed lazily by the allocator (later unit).
  for (size_t i = 1; i <= LOWFAT_NUM_REGIONS; i++) {
    void *heap_start = (uint8_t *)lowfat_region(i) + LOWFAT_HEAP_MEMORY_OFFSET;
    void *ptr = lowfat_map(heap_start, LOWFAT_HEAP_MEMORY_SIZE, false, false);
    if (ptr != heap_start)
      lowfat_init_error("failed to reserve region");
  }
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
