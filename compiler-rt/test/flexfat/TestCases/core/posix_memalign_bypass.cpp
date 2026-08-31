// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

#include <cstdint>
#include <cstdio>
#include <cstdlib>

using uptr = uintptr_t;

extern "C" uptr __flexfat_get_base(uptr ptr);
extern "C" uptr __flexfat_get_size(uptr ptr);

int main() {
  void *p = nullptr;
  if (posix_memalign(&p, 64, 32) != 0)
    return 1;

  if (__flexfat_get_base((uptr)p) == 0)
    return 2;
  if (__flexfat_get_size((uptr)p) == (uptr)-1)
    return 3;

  // CHECK: posix_memalign_managed: ok
  printf("posix_memalign_managed: ok\n");
  free(p);
  return 0;
}
