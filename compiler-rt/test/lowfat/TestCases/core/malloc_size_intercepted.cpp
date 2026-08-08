// RUN: %clangxx_lowfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_lowfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s
// REQUIRES: system-darwin

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <malloc/malloc.h>

using uptr = uintptr_t;

extern "C" uptr __lf_get_size(uptr ptr);

int main() {
  void *p = malloc(48);
  if (!p) return 1;

  size_t runtime_size = malloc_size(p);
  uptr lowfat_size = __lf_get_size((uptr)p);
  if (runtime_size != lowfat_size) return 2;

  // CHECK: malloc_size_intercepted: ok
  printf("malloc_size_intercepted: ok\n");
  free(p);
  return 0;
}
