// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O1 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <cstdlib>
#include <cstring>

int main() {
  char *dst = (char *)malloc(15);
  char *guard = (char *)malloc(15); // keep adjacent memory mapped
  if (!dst || !guard) return 1;

  const char payload[32] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

  // CHECK: FLEXFAT ERROR: out-of-bounds error detected!
  memcpy(dst, payload, 32);
  // Keep the write observable until late instrumentation.
  asm volatile("" : : "r"(dst) : "memory");

  free(guard);
  free(dst);
  return 0;
}
