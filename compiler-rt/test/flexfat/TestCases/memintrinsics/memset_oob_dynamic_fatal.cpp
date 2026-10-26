// RUN: %clangxx_flexfat -fno-builtin-memset -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -fno-builtin-memset -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  char *buf = (char *)malloc(15);
  if (!buf) return 1;

  size_t size = 16 + argc; // 17

  // CHECK: FLEXFAT ERROR: out-of-bounds error detected!
  // CHECK: operation = write
  // CHECK: size      = 15
  // CHECK: overflow  = +2
  memset(buf, 'A', size);

  free(buf);
  return 0;
}
