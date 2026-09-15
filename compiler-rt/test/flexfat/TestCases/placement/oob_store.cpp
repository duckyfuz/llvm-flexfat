// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=optimizer-early %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-EARLY-MISS
// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=scalar-late -mllvm -flexfat-check-whole-access %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-LATE-CATCH

// The late placement sees this OOB store, while the early placement does not.

#include <cstdio>
#include <cstdlib>

__attribute__((noinline)) static void write_past_end(char *p) {
  *reinterpret_cast<double *>(p + 14) = 42.0;
}

int main() {
  char *p = static_cast<char *>(malloc(16));
  write_past_end(p);
  free(p);
  std::puts("DONE");
}

// CHECK-EARLY-MISS: DONE
// CHECK-LATE-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
