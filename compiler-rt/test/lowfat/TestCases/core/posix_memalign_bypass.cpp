// RUN: %clangxx_lowfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_lowfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

#include <cstdio>
#include <cstdlib>
#include <cstdint>

using uptr = uintptr_t;

extern "C" uptr __lf_get_base(uptr ptr);
extern "C" uptr __lf_get_size(uptr ptr);

int main() {
  void *p = nullptr;
  if (posix_memalign(&p, 64, 32) != 0) return 1;

  if (__lf_get_base((uptr)p) != 0) return 2;
  if (__lf_get_size((uptr)p) != (uptr)-1) return 3;

  // CHECK: posix_memalign_bypass: ok
  printf("posix_memalign_bypass: ok\n");
  free(p);
  return 0;
}
