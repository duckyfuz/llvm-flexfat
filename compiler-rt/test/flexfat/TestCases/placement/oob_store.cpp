// RUN: %clangxx_flexfat -O0 -mllvm -flexfat-check-whole-access %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CATCH
// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-check-whole-access %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=MISS

// The store is checked at O0. At O3 its effects are discarded before
// OptimizerLastEP because the allocation is freed without being read.

#include <cstdio>
#include <cstdlib>

__attribute__((noinline)) static void write_past_end(char *p) {
  *reinterpret_cast<double *>(p + 14) = 42.0;
}

int main() {
  char *p = static_cast<char *>(malloc(15));
  write_past_end(p);
  free(p);
  std::puts("DONE");
}

// MISS: DONE
// CATCH: FLEXFAT ERROR: out-of-bounds error detected!
