// RUN: %clang_lowfat %s -o %t && not %t 2>&1 | FileCheck %s
//
// Security test: write past allocation boundary is detected.

#include <stdio.h>
#include <stdlib.h>

int main() {
  volatile char *p = (volatile char *)malloc(32);
  if (!p) return 1;

  // Write within bounds.
  for (int i = 0; i < 32; i++)
    p[i] = (char)i;

  // Write exactly at the boundary — OOB.
  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  // CHECK: operation = write
  p[32] = 'X';

  printf("should not reach here\n");
  free((void *)p);
  return 0;
}
