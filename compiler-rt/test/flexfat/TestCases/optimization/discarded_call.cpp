// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O3 %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// A discarded read is checked at O0, but can be removed by optimization
// before FlexFat instruments at OptimizerLastEP. Whole-access checking is
// enabled in both runs so the difference comes from optimization timing.

#include <cstdio>
#include <cstdlib>

// noinline keeps this as an inter-procedural case.
__attribute__((noinline))
static double peek(char *p) {
  // 8-byte (double) OOB read. p was allocated with malloc(15); a double starting
  // at offset 14 spans bytes [14, 22), which overflows the 16-byte FlexFat slot
  // boundary at byte 16. FlexFat detects this as an out-of-bounds access.
  return *reinterpret_cast<double *>(p + 14);
}

int main() {
  char *p = (char *)malloc(15);
  peek(p);   // Return value intentionally discarded.
  free(p);

  // CHECK-MISS: DONE
  // CHECK-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
  printf("DONE\n");
  return 0;
}
