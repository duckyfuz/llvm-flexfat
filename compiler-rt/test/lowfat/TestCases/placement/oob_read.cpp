// RUN: %clangxx_lowfat -O3 -mllvm -lowfat-placement=optimizer-early %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_lowfat -O3 -mllvm -lowfat-placement=scalar-late %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS

// Compare an OOB read that both placements currently miss at -O3.

#include <cstdio>
#include <cstdlib>

__attribute__((noinline)) static double read_past_end(const char *p) {
  return *reinterpret_cast<const double *>(p + 14);
}

int main() {
  char *p = static_cast<char *>(malloc(16));
  std::printf("%f\n", read_past_end(p));
  free(p);
  std::puts("DONE");
}

// CHECK-MISS: DONE
