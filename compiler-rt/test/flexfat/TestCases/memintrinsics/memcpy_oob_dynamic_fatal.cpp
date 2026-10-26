// RUN: %clangxx_flexfat -fno-builtin-memcpy -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -fno-builtin-memcpy -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <string.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  char *dst = (char *)malloc(15);
  char *src = strdup("ABCDEFGHIJKLMNOPQRS"); // 19 bytes
  if (!dst || !src) return 1;

  size_t size = 16 + argc; // 17

  // CHECK: FLEXFAT ERROR: out-of-bounds error detected!
  // CHECK: operation = write
  // CHECK: size      = 15
  // CHECK: overflow  = +2
  memcpy(dst, src, size);

  free(dst);
  free(src);
  return 0;
}
