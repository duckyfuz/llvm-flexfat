// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-ALL

// A heap read whose value remains live is detected with whole-access checking.

#include <cstdio>
#include <cstdlib>

__attribute__((noinline))
double oob_read(char *p) {
  // 8-byte read at offset 14 of a 16-byte slot.
  // Bytes [14, 22) exceed the slot boundary [0, 16): genuine OOB.
  return *(double *)(p + 14);
}

int main() {
  char *p = (char *)malloc(15);
  double val = oob_read(p);  // Return value kept live; load is not DCE'd.
  // CHECK-ALL: FLEXFAT ERROR: out-of-bounds error detected!
  printf("val=%f\n", val);
  free(p);
  return 0;
}
