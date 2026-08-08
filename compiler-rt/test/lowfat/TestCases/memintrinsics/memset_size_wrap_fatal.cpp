// RUN: %clangxx_lowfat -fno-builtin-memset -O0 %s -o %t
// RUN: %clangxx_lowfat -fno-builtin-memset -O1 %s -o %t
// RUN: %clangxx_lowfat -fno-builtin-memset -O2 %s -o %t
// RUN: %clangxx_lowfat -fno-builtin-memset -O3 %s -o %t
// RUN: not %run %t 2>&1 | FileCheck %s

// A wrapped dynamic size must still be rejected by the runtime interceptor
// before libc sees it.

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int main() {
  char *dst = (char *)malloc(16);
  if (!dst)
    return 1;

  volatile size_t size = ~(size_t)0;

  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  // CHECK: operation = write
  // CHECK: overflow = +1
  memset(dst, 0, size);

  free(dst);
  return 0;
}
