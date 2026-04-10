// RUN: %clang_lowfat -O0 %s -o %t && not %t 2>&1 | FileCheck %s
//
// Security test: GEP instrumentation catches pointer arithmetic that crosses
// a slot boundary before any load/store occurs.

#include <stdio.h>
#include <stdlib.h>

int main() {
  volatile int *p = (volatile int *)malloc(16);
  if (!p) return 1;

  // 16 / 4 = 4 valid int elements (indices 0-3).
  // GEP to index 4 = 16 bytes past start = at slot boundary.
  // The GEP check should catch this.
  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  volatile int *q = &p[4];

  int val = *q;
  printf("val = %d\n", val);
  free((void *)p);
  return 0;
}
