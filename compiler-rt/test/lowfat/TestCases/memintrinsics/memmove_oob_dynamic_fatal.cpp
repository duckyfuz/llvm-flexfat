// RUN: %clang_lowfat %s -o %t && not %t 2>&1 | FileCheck %s
//
// Verify OOB memmove with dynamic size is caught.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  char *dst = (char *)malloc(16);
  char *src = (char *)malloc(64);
  if (!dst || !src) return 1;

  memset(src, 'C', 64);

  // Dynamic size that overflows dst. argc prevents constant folding.
  size_t n = 16 + argc;

  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  memmove(dst, src, n);

  printf("should not reach here\n");
  free(dst);
  free(src);
  return 0;
}
