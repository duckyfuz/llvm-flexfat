// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -mllvm -flexfat-tbi-storage=last-byte %flexfat_config_flags -fno-builtin %s -o %t
// RUN: %run %t
// RUN: %run %t onepast
// RUN: %run %t maps
// RUN: %run %t wide
// RUN: not %run %t byte 2>&1 | FileCheck %s
// RUN: not %run %t memset 2>&1 | FileCheck %s
// CHECK: out-of-bounds
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include "flexfat/flexfat_config.h"

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);
extern "C" uintptr_t __flexfat_get_usable_size(uintptr_t);
extern "C" void *__interceptor_memset(void *, int, size_t);

__attribute__((noinline)) static char *opaque(char *p) {
  asm volatile("" : "+r"(p) : : "memory");
  return p;
}

int main(int argc, char **argv) {
  char *p = opaque((char *)malloc(15));
  assert(p);
  uintptr_t base = __flexfat_get_base((uintptr_t)p) & 0x00ffffffffffffffULL;
  size_t size = __flexfat_get_size((uintptr_t)p);
  assert(size == 16);
  assert(__flexfat_get_usable_size((uintptr_t)p) == 15);
  assert(malloc_usable_size(p) == 15);
  char *last = opaque((char *)((uintptr_t)p + size - 1));
  if (argc > 1 && !strcmp(argv[1], "byte"))
    return *(volatile char *)last;
  if (argc > 1 && !strcmp(argv[1], "wide")) {
    // Default scalar checks test the starting byte. The wide read overlaps
    // the reserved generation byte without starting at it.
    (void)*(volatile int *)(last - 3);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "memset")) {
    __interceptor_memset(p, 0, size);
    return 0;
  }
  if (argc > 1 && !strcmp(argv[1], "onepast")) {
    char *end = opaque(p + 15);
    assert(end == last);
  }
  if (argc > 1 && !strcmp(argv[1], "maps")) {
    uintptr_t forbidden;
#ifdef FLEXFAT_CUSTOM_CONFIG
    forbidden = __flexfat::kTablesBase + 2 * __flexfat::kTablesOffset;
#else
    forbidden = ((__flexfat::kRegionBase >> __flexfat::kRegionSizeLog) - 1) *
                (1ULL << (__flexfat::kRegionSizeLog - __flexfat::kMinSizeLog));
#endif
    FILE *maps = fopen("/proc/self/maps", "r");
    assert(maps);
    char line[512];
    while (fgets(line, sizeof(line), maps)) {
      unsigned long start, end;
      if (sscanf(line, "%lx-%lx", &start, &end) == 2)
        assert(!(start <= forbidden && forbidden < end));
    }
    fclose(maps);
  }
  char *q = opaque((char *)malloc(15));
  if (argc > 1 && (!strcmp(argv[1], "adjacent-tag") ||
                   !strcmp(argv[1], "recover-other"))) {
    char *slots[256];
    for (char *&slot : slots)
      slot = opaque((char *)malloc(15));
    for (unsigned i = 1; i < 256; ++i) {
      uintptr_t before = (uintptr_t)slots[i - 1] & 0x00ffffffffffffffULL;
      uintptr_t after = (uintptr_t)slots[i] & 0x00ffffffffffffffULL;
      if (after == before + 16 &&
          ((uintptr_t)slots[i] >> 56) == ((uintptr_t)slots[i - 1] >> 56)) {
        if (!strcmp(argv[1], "adjacent-tag"))
          *(volatile char *)opaque((char *)((uintptr_t)slots[i] - 1)) = 0;
        else
          *(volatile char *)(slots[i] - 2) = 1;
        return 0;
      }
    }
    abort();
  }
  unsigned q_tag = (uintptr_t)q >> 56;
  unsigned old_tag = (uintptr_t)p >> 56;
  free(p);
  char *again = opaque((char *)malloc(15));
  assert(((uintptr_t)again & 0x00ffffffffffffffULL) == base);
  assert(((uintptr_t)again >> 56) != old_tag);
  assert(((uintptr_t)q >> 56) == q_tag);
  *(volatile char *)q = 7;
  assert(*(volatile char *)q == 7);
  free(q);
  free(again);
}
