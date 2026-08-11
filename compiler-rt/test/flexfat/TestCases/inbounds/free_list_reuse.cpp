// RUN: %clangxx_flexfat -O0 %s -o %t
// RUN: %clangxx_flexfat -O1 %s -o %t
// RUN: %clangxx_flexfat -O2 %s -o %t
// RUN: %clangxx_flexfat -O3 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

// Free-list reuse sanity test.

#include <cstdio>

extern "C" void *__flexfat_malloc(unsigned long size);
extern "C" void __flexfat_free(void *ptr);

int main() {
  // Allocate and free, then allocate again.
  int *a = (int *)__flexfat_malloc(10 * sizeof(int));
  a[0] = 1;
  __flexfat_free(a);

  int *b = (int *)__flexfat_malloc(10 * sizeof(int));
  b[0] = 2;
  b[9] = 3;
  __flexfat_free(b);

  // CHECK: free_list_reuse: ok
  // CHECK-NOT: FLEXFAT ERROR
  printf("free_list_reuse: ok\n");
  return 0;
}
