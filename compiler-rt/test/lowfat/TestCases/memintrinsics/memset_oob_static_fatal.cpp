// RUN: %clang_lowfat %s -o %t && not %t 2>&1 | FileCheck %s
//
// Verify that an OOB memset is caught.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  char *p = (char *)malloc(16);
  if (!p) return 1;

  // Memset 32 bytes into a 16-byte buffer.
  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  memset(p, 0, 32);

  printf("should not reach here\n");
  free(p);
  return 0;
}
