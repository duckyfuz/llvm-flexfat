// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -O2 -fno-vectorize -fno-slp-vectorize -fno-unroll-loops %s -o %t && %t
// RUN: not %t stale 2>&1 | FileCheck %s
// RUN: not %t overflow 2>&1 | FileCheck %s
// CHECK: temporal violation
// CHECK: generation mismatch
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

__attribute__((noinline)) uint64_t scan(uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += p[i];
  return sum;
}
__attribute__((noinline)) uint64_t backwards(uint64_t *p, size_t n) {
  uint64_t sum = 0;
  for (size_t i = 0; i < n; ++i) sum += p[-(intptr_t)i];
  return sum;
}
__attribute__((noinline)) void update(uint64_t *a, const uint64_t *b, size_t n) {
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
int main(int argc, char **argv) {
  uint64_t *a = (uint64_t *)malloc(32*sizeof(uint64_t));
  uint64_t *b = (uint64_t *)malloc(32*sizeof(uint64_t));
  uint64_t *foreign = (uint64_t *)mmap(nullptr, 4096, PROT_READ|PROT_WRITE,
                                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  assert(a && b && foreign != MAP_FAILED);
  for (size_t i = 0; i < 32; ++i) a[i] = b[i] = foreign[i] = i+1;
  if (argc > 1) {
    if (!strcmp(argv[1], "call")) return after_free(a, 3);
    free(a);
    if (!strcmp(argv[1], "overflow")) return scan(a, UINT64_C(1)<<62);
    if (!strcmp(argv[1], "write")) { update(a, b, 8); return 0; }
    return scan(a, 8);
  }
  assert(scan((uint64_t *)UINT64_MAX, 0) == 0);
  assert(scan(a, 32) == 528);
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
  free(a); free(b); munmap(foreign, 4096);
}
