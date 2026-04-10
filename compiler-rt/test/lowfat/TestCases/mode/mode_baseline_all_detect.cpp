// RUN: %clang_lowfat -O2 %s -o %t && not %t 2>&1 | FileCheck %s
//
// Basic OOB detection at -O2 in default (fast) mode.

#include <stdio.h>
#include <stdlib.h>

int main() {
  volatile int *p = (volatile int *)malloc(sizeof(int) * 4);
  if (!p) return 1;

  p[0] = 10;
  p[1] = 20;
  p[2] = 30;
  p[3] = 40;

  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  int val = p[4];

  printf("val = %d\n", val);
  free((void *)p);
  return 0;
}
