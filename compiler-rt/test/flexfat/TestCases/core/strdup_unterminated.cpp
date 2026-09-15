// RUN: %clangxx_flexfat -fno-builtin-strdup -O0 %s -o %t
// RUN: not %run %t 2>&1 | FileCheck %s

#include <cstdlib>
#include <cstring>

int main() {
  char *source = (char *)malloc(16);
  memset(source, 'x', 16);
  char *copy = strdup(source);
  free(copy);
  free(source);
  return 0;
}

// CHECK: FLEXFAT ERROR: out-of-bounds error detected!
// CHECK: operation = read
