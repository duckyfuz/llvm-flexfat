// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=optimizer-last %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// An out-of-bounds read inside peek(), with the return value discarded.
// With the legacy optimizer-last placement, the dead computation can be
// optimized away before the FlexFat pass runs. In safe mode, early
// instrumentation preserves the OOB check even when the helper is inlined.

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
