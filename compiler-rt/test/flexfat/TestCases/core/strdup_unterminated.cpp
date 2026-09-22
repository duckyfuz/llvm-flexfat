// RUN: %clangxx_flexfat -fno-builtin-strdup -O0 %s -o %t
// RUN: not %run %t 2>&1 | FileCheck %s

#include <cstdint>
#include <cstdlib>
extern "C" size_t __flexfat_get_usable_size(uintptr_t);
#include <cstring>

int main() {
  char *source = (char *)malloc(16);
  memset(source, 'x', __flexfat_get_usable_size((uintptr_t)source));
  char *copy = strdup(source);
  free(copy);
  free(source);
  return 0;
}

// CHECK: FLEXFAT ERROR: out-of-bounds error detected!
// CHECK: operation = read
