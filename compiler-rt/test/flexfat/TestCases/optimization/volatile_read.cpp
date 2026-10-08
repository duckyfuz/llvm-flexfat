// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-OOB
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-OOB

// A volatile heap read remains observable and is checked at O0 and O2.

#include <cstdio>
#include <cstdlib>

volatile char sink; // volatile global: any write/read here is always observable

int main() {
  char *p = (char *)malloc(15);

  // 8-byte (double) OOB read at offset 14: bytes [14, 22) overflow the
  // 16-byte FlexFat slot [0, 16).
  sink = (char)(*reinterpret_cast<volatile double *>(p + 14));

  free(p);

  // CHECK-OOB: FLEXFAT ERROR: out-of-bounds error detected!
  printf("DONE\n");
  return 0;
}
