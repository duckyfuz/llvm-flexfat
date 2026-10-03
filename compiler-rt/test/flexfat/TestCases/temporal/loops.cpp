// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -mllvm -flexfat-version-tbi-loops=true -O2 -fno-vectorize -fno-slp-vectorize -fno-unroll-loops %s -o %t && %t
// RUN: not %t stale 2>&1 | FileCheck %s
// RUN: not %t overflow 2>&1 | FileCheck %s
// RUN: not %t quad-call 2>&1 | FileCheck %s
// RUN: not %t quad-cross 2>&1 | FileCheck %s --check-prefix=CROSS
// RUN: not %t quad-overflow 2>&1 | FileCheck %s --check-prefix=CROSS
// CHECK: temporal violation
// CHECK: generation mismatch
// CROSS: out-of-bounds error detected
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);

__attribute__((noinline)) uint64_t scan(volatile uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += p[i];
  return sum;
}
__attribute__((noinline)) uint64_t backwards(volatile uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += p[-(intptr_t)i];
  return sum;
}
__attribute__((noinline)) void update(volatile uint64_t *a, const volatile uint64_t *b, size_t n) {
  for (size_t i = 0; i < n; ++i) a[i] += b[i];
}
__attribute__((noinline)) uint64_t after_free(uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) {
    if (i == 1) free(p);
    sum += p[i];
  }
  return sum;
}
__attribute__((noinline)) uint64_t fixed(volatile uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += *p;
  return sum;
}
__attribute__((noinline)) void reverse_write(volatile uint64_t *p, size_t n) {
  for (size_t i = 0; i < n; ++i) p[-(intptr_t)i] = 7;
}
__attribute__((noinline)) void fixed_write(volatile uint64_t *p, size_t n) {
  for (size_t i = 0; i < n; ++i) *p = i;
}
__attribute__((noinline)) uint64_t ordinary(uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += p[i];
  return sum;
}
__attribute__((noinline)) uint64_t four_offsets(volatile uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) {
    if (i & 1) sum += p[i];
    sum += p[i+1] + p[i+2] + p[i+3] + p[i+4];
  }
  return sum;
}
__attribute__((noinline)) uint64_t four_reverse(volatile uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i)
    sum += p[-(intptr_t)i] + p[1-(intptr_t)i] +
           p[2-(intptr_t)i] + p[3-(intptr_t)i];
  return sum;
}
__attribute__((noinline)) uint64_t four_after_free(uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) {
    if (i == 1) free(p);
    sum += p[i] + p[i+1] + p[i+2] + p[i+3];
  }
  return sum;
}
int main(int argc, char **argv) {
  uint64_t *a = (uint64_t *)malloc(32*sizeof(uint64_t));
  uint64_t *b = (uint64_t *)malloc(32*sizeof(uint64_t));
  uint64_t *foreign = (uint64_t *)mmap(nullptr, 4096, PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(a && b && foreign != MAP_FAILED);
  for (size_t i = 0; i < 32; ++i) a[i] = b[i] = foreign[i] = i+1;
  if (argc > 1) {
    if (!strcmp(argv[1], "width")) {
      uintptr_t base = __flexfat_get_base((uintptr_t)a);
      uintptr_t size = __flexfat_get_size((uintptr_t)a);
      return scan((uint64_t *)(base + size - 4), 1);
    }
    if (!strncmp(argv[1], "geometry", 8)) {
      for (unsigned n = 33; n < 1000; n += 16) {
        void *q = malloc(n);
        uintptr_t region = ((uintptr_t)q & 0x00ffffffffffffffULL) & ~((1ULL << 38)-1);
        uintptr_t size = __flexfat_get_size((uintptr_t)q);
        uintptr_t edge = strstr(argv[1], "tail") ? region + (1ULL << 38) : region;
        if (edge % size)
          return scan((uint64_t *)((1ULL << 56) | (strstr(argv[1], "tail") ? edge-1 : edge)), 1);
        free(q);
      }
      abort();
    }
    if (!strcmp(argv[1], "call")) return after_free(a, 3);
    if (!strcmp(argv[1], "quad-call")) return four_after_free(a, 3);
    if (!strcmp(argv[1], "quad-cross")) {
      uintptr_t base = __flexfat_get_base((uintptr_t)a);
      uintptr_t size = __flexfat_get_size((uintptr_t)a);
      return four_offsets((uint64_t *)(base + size - 32), 5);
    }
    if (!strcmp(argv[1], "quad-overflow"))
      return four_offsets(a, UINT64_C(1)<<62);
    free(a);
    if (!strcmp(argv[1], "reuse")) {
      volatile uint64_t *replacement = (uint64_t *)malloc(32*sizeof(uint64_t));
      assert(((uintptr_t)replacement & 0x00ffffffffffffffULL) ==
             ((uintptr_t)a & 0x00ffffffffffffffULL));
      *replacement = 42;
      return scan(a, 8);
    }
    if (!strcmp(argv[1], "fixed")) return fixed(a, 8);
    if (!strcmp(argv[1], "overflow")) return scan(a, UINT64_C(1)<<62);
    if (!strcmp(argv[1], "write")) { update(a, b, 8); return 0; }
    return scan(a, 8);
  }
  assert(scan((uint64_t *)UINT64_MAX, 0) == 0);
  assert(four_offsets((uint64_t *)UINT64_MAX, 0) == 0);
  assert(fixed(a, 8) == 8);
  assert(fixed((uint64_t *)UINT64_MAX, 0) == 0);
  assert(ordinary(a, 32) == 528);
  assert(scan(a, 32) == 528);
  assert(four_offsets(a, 28) == 2114);
  assert(four_reverse(a+28, 26) == 1872);
  assert(four_offsets(foreign, 28) == 2114);
  assert(scan(a+4, 8) == 68);
  assert(backwards(a+11, 8) == 68);
  assert(scan(foreign, 32) == 528); // Guard fails normally for unmanaged root.
  auto *tagged = (uint64_t *)((uintptr_t)foreign | (UINT64_C(0xab)<<56));
  assert(scan(tagged, 32) == 528); // Branchless unmanaged sentinel bypass.
  update(a, b, 32);              // Successful multiple-root guard.
  assert(scan(a, 32) == 1056);
  update(a, foreign, 32);        // Mixed roots force ordinary fallback.
  assert(scan(a, 32) == 1584);
  update(a, b, 1);               // Short-loop guard.
  reverse_write(a+11, 8);
  assert(scan(a+4, 8) == 56);
  fixed_write(a, 8);
  assert(*a == 7);
  reverse_write((uint64_t *)UINT64_MAX, 0);
  fixed_write((uint64_t *)UINT64_MAX, 0);
  free(a); free(b); munmap(foreign, 4096);
}
