// RUN: %clang_lowfat_recover %s -o %t && %t 2>&1 | FileCheck %s
//
// Verify that in recover mode, an OOB memcpy warns but continues.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  char *dst = (char *)malloc(16);
  char *src = (char *)malloc(32);
  if (!dst || !src) return 1;

  memset(src, 'B', 32);

  // CHECK: LOWFAT WARNING: out-of-bounds error detected!
  memcpy(dst, src, 32);

  // Should reach here in recover mode.
  // CHECK: continued after warning
  printf("continued after warning\n");
  free(dst);
  free(src);
  return 0;
}
