// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

// All requested bytes must remain accessible with slot-base placement.

#include <cstdio>
#include <cstdlib>

int main() {
  // 48 bytes -> 64-byte class in the POW2 configuration.
  char *p = (char *)malloc(48);
  if (!p) return 1;

  // Write every byte of the requested allocation.
  for (int i = 0; i < 48; i++)
    p[i] = (char)i;

  // Read back and verify.
  for (int i = 0; i < 48; i++)
    if (p[i] != (char)i) return 2;

  // CHECK: inbounds: ok
  // CHECK-NOT: FLEXFAT ERROR
  printf("inbounds: ok\n");
  free(p);
  return 0;
}
