// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// REQUIRES: flexfat-custom-config

// Reduced 483.xalancbmk-style case: retain the original 48-byte allocation as
// the companion base and prove that a 24-byte overread is not mistaken for a
// valid pointer in the following custom-layout slot.

#include <cstdlib>

__attribute__((noinline)) static void consume(char *p) {
  asm volatile("" : : "r"(p) : "memory");
}

int main() {
  volatile size_t runtimeSize = 48;
  char *base = static_cast<char *>(std::malloc(runtimeSize));
  if (!base)
    return 1;
  consume(base + runtimeSize + 24);
  return 0;
}

// CHECK: FLEXFAT ERROR: out-of-bounds error detected!
