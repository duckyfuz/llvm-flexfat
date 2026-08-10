// RUN: %clangxx_lowfat -O3 -mllvm -lowfat-placement=optimizer-early %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_lowfat -O3 -mllvm -lowfat-placement=scalar-late %s -o %t && not %run %t 2>&1 | FileCheck %s

// LowFat instruments pointer arithmetic as well as memory accesses. Keep the
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

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
