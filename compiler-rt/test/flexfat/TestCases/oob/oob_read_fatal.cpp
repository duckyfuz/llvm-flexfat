// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O1 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s

// OOB scalar read across an allocation boundary must be reported in fatal mode.

#include <cstdlib>

int main() {
  char *buf = (char *)malloc(31);
  if (!buf) return 1;

  buf[0] = 'H';
  buf[30] = 'i';

  // Read 8 bytes at offset 28 of a 32-byte slot:
  //   bytes 28-35 exceed the 32-byte boundary: OOB.
  // CHECK: FLEXFAT ERROR: out-of-bounds error detected!
  volatile double *p = (volatile double *)(buf + 28);
  double val = *p; // 8-byte read at offset 28 of 32-byte alloc: OOB (bytes 28-35)
  (void)val;       // The volatile load survives optimization.

  free(buf);
  return 0;
}
