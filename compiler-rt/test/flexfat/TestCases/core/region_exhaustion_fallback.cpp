// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t | FileCheck %s

#include "Inputs/exhaust-managed-region.h"
#include <cstdio>

int main() {
  void *slots[512];
  unsigned count = exhaust_managed_region(slots, 512);
  *static_cast<volatile char *>(slots[count - 1]) = 7;
  for (unsigned i = 0; i != count; ++i)
    free(slots[i]);
  puts("region_exhaustion_fallback: ok");
}
// CHECK: region_exhaustion_fallback: ok
