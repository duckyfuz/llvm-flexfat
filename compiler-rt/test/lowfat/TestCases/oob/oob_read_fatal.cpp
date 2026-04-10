// RUN: %clang_lowfat %s -o %t && not %t 2>&1 | FileCheck %s
//
// Verify that an out-of-bounds read is detected and reported.

#include <stdio.h>
#include <stdlib.h>

int main() {
  // Allocate 16 bytes (smallest size class).
  volatile char *p = (volatile char *)malloc(16);
  if (!p) return 1;

  // Access 1 byte past the 16-byte slot boundary.
  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  // CHECK: operation = read
  char val = p[16];

  // Should not reach here.
  printf("val = %d\n", val);
  free((void *)p);
  return 0;
}
