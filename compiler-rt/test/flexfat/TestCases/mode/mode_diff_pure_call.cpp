// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=optimizer-last %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// Mode-difference test for discarded return values at -O3.
// With the legacy optimizer-last placement, DAE can remove the load before
// instrumentation, so the OOB read is missed. In safe mode
// (%clangxx_flexfat_safe), PipelineStartEP instrumentation keeps the load alive
// and the OOB read is reported.

#include <cstdio>
#include <cstdlib>

// noinline keeps this as an inter-procedural case.
__attribute__((noinline))
static double peek(char *p) {
  // 8-byte (double) OOB read. p was allocated with malloc(16); a double starting
  // at offset 14 spans bytes [14, 22), which overflows the 16-byte FlexFat slot
  // boundary at byte 16. FlexFat detects this as an out-of-bounds access.
  return *reinterpret_cast<double *>(p + 14);
}

int main() {
  char *p = (char *)malloc(16);
  peek(p);   // Return value intentionally discarded.
  free(p);

  // CHECK-MISS: DONE
  // CHECK-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
  printf("DONE\n");
  return 0;
}
