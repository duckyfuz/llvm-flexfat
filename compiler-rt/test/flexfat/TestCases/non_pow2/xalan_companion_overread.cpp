// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// REQUIRES: flexfat-custom-config

#include <cstdlib>
#include <cassert>
#include <cstdint>
extern "C" uintptr_t __flexfat_get_size(uintptr_t);

__attribute__((noinline)) static void consume(char *p) {
  asm volatile("" : : "r"(p) : "memory");
}

int main() {
  volatile size_t runtimeSize = 47;
  char *base = static_cast<char *>(std::malloc(runtimeSize));
  if (!base)
    return 1;
  size_t slot = __flexfat_get_size((uintptr_t)base);
  assert(slot == 48);
  // Retain the original companion base across an escape into the next slot.
  consume(base + slot + 24);
  return 0;
}

// CHECK: FLEXFAT ERROR: out-of-bounds error detected!
