// RUN: %clang_lowfat_safe -O2 %s -o %t && not %t 2>&1 | FileCheck %s
//
// In Safe mode, loads must not be eliminated by the optimizer even if their
// results are unused. This test verifies that the OOB load is still detected
// at -O2 with Safe mode.

#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline))
int read_oob(volatile char *p) {
  // The optimizer may try to eliminate this dead load.
  // Safe mode barriers should prevent that.
  return p[16];
}

int main() {
  volatile char *p = (volatile char *)malloc(16);
  if (!p) return 1;

  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  read_oob(p);

  printf("should not reach here\n");
  free((void *)p);
  return 0;
}
