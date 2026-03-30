// RUN: %clangxx_lowfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_lowfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

#include <cstdio>
#include <cstdlib>
#include <cstdint>

using uptr = uintptr_t;

extern "C" uptr __lf_get_size(uptr ptr);

int main() {
  unsigned char *p = (unsigned char *)calloc(8, 4);
  if (!p) return 1;

  if (__lf_get_size((uptr)p) == (uptr)-1) return 2;

  for (int i = 0; i < 32; ++i)
    if (p[i] != 0) return 3;

  // CHECK: calloc_zeroed: ok
  printf("calloc_zeroed: ok\n");
  free(p);
  return 0;
}
