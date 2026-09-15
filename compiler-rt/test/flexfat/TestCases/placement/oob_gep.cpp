// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=optimizer-early %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=scalar-late %s -o %t && %run %t | FileCheck %s

// Exact one-past formation and escape are valid at both pass placements.

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

// CHECK: 0x
