// RUN: %clangxx_flexfat -O0 %s -o %t
// RUN: %clangxx_flexfat -O1 %s -o %t
// RUN: %clangxx_flexfat -O2 %s -o %t
// RUN: %clangxx_flexfat -O3 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

// Basic in-bounds allocation test.

#include <cstdio>

extern "C" void *__flexfat_malloc(unsigned long size);
extern "C" void __flexfat_free(void *ptr);

int main() {
  int *arr = (int *)__flexfat_malloc(10 * sizeof(int));
  if (!arr)
    return 1;

  // In-bounds accesses should not trigger OOB.
  arr[0] = 42;
  arr[9] = 99;

  __flexfat_free(arr);
  // CHECK: basic_inbounds: ok
  // CHECK-NOT: FLEXFAT ERROR
  printf("basic_inbounds: ok\n");
  return 0;
}
