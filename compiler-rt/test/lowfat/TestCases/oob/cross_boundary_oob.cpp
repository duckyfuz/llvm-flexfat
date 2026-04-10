// RUN: %clang_lowfat %s -o %t && not %t 2>&1 | FileCheck %s
//
// Verify detection of a large stride overflow that crosses a slot boundary.

#include <stdio.h>
#include <stdlib.h>

int main() {
  volatile int *p = (volatile int *)malloc(32);
  if (!p) return 1;

  // 32 / sizeof(int) = 8 valid elements.
  // Access element 8 = 32 bytes past the start = exactly at slot boundary.
  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  int val = p[8];

  printf("val = %d\n", val);
  free((void *)p);
  return 0;
}
