// RUN: %clang_lowfat %s -o %t && not %t 2>&1 | FileCheck %s
//
// Verify that an OOB memcpy is caught by the interceptor.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  char *dst = (char *)malloc(16);
  char *src = (char *)malloc(32);
  if (!dst || !src) return 1;

  memset(src, 'A', 32);

  // Copy 32 bytes into a 16-byte buffer — OOB on dst.
  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  memcpy(dst, src, 32);

  printf("should not reach here\n");
  free(dst);
  free(src);
  return 0;
}
