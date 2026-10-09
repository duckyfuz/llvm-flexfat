// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O3 %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// A discarded read is checked at O0, but can be removed by optimization
// before FlexFat instruments at OptimizerLastEP. Whole-access checking is
// enabled in both runs so the difference comes from optimization timing.

#include <cstdlib>
#include <cstdio>

double peek(char *p) {
  return *(double *)(p + 14);
}

int main() {
  char *p = (char *)malloc(8);
  peek(p);
  // CHECK-MISS: DONE
  // CHECK-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
  printf("DONE\n");
  return 0;
}
