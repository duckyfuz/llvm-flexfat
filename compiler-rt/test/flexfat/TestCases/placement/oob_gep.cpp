// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=optimizer-early %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=scalar-late %s -o %t && not %run %t 2>&1 | FileCheck %s

// FlexFat instruments pointer arithmetic as well as memory accesses. Keep the
// derived pointer observable so the optimizer cannot discard the GEP itself.

#include <cstdio>
#include <cstdlib>

__attribute__((noinline)) static void expose(char *p) {
  std::printf("%p\n", static_cast<void *>(p + 16));
}

int main() {
  char *p = static_cast<char *>(malloc(16));
  expose(p);
  free(p);
}

// CHECK: FLEXFAT ERROR: out-of-bounds error detected!
