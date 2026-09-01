// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// REQUIRES: flexfat-custom-config

#include <cstdint>
#include <cstdlib>
#include <unistd.h>

using uptr = uintptr_t;
extern "C" uptr __flexfat_get_base(uptr);

int main() {
  void *slots[257];
  for (unsigned i = 0; i != 257; ++i) {
    slots[i] = malloc(1ULL << 30);
    if (!slots[i])
      return 1;
  }
  if (__flexfat_get_base((uptr)slots[256]) != 0)
    return 2;
  *(volatile char *)slots[256] = 7;
  for (void *slot : slots)
    free(slot);
  // CHECK: region_exhaustion_fallback: ok
  write(1, "region_exhaustion_fallback: ok\n", 31);
  return 0;
}
