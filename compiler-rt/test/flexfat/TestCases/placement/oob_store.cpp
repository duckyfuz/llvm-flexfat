// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-mode=fast -mllvm -flexfat-check-whole-access %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-LATE-CATCH

// Whole-access checking catches the store crossing the slot boundary.

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

// CHECK-EARLY-MISS: DONE
// CHECK-LATE-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
