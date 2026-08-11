// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

#include <cstdio>
#include <cstdlib>
#include <cstdint>

using uptr = uintptr_t;

extern "C" uptr __flexfat_get_size(uptr ptr);

int main() {
  char *p = (char *)realloc(nullptr, 17);
  if (!p) return 1;

  uptr size = __flexfat_get_size((uptr)p);
  if (size == (uptr)-1) return 2;
  if (size < 17) return 3;

  p[16] = 'x';

  // CHECK: realloc_nullptr: ok
  printf("realloc_nullptr: ok\n");
  free(p);
  return 0;
}
