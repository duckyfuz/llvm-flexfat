// RUN: %clangxx_flexfat -fno-builtin-memset -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -fno-builtin-memset -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main() {
  char *dst = (char *)malloc(16);
  if (!dst)
    return 1;

  volatile size_t size = ~(size_t)0;

  // CHECK: FLEXFAT ERROR: out-of-bounds error detected!
  // CHECK: operation = write
  // CHECK: overflow = +1
  memset(dst, 0, size);

  free(dst);
  return 0;
}
