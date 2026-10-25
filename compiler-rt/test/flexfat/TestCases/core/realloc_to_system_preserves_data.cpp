// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t | FileCheck %s

#include "Inputs/exhaust-managed-region.h"
#include <cstdio>

int main() {
  char *p = static_cast<char *>(malloc(175));
  char *neighbor = static_cast<char *>(malloc(175));
  assert(p && neighbor);
  for (unsigned i = 0; i != 175; ++i) {
    p[i] = static_cast<char>(i);
    neighbor[i] = 0;
  }
  asm volatile("" : : "r"(p), "r"(neighbor) : "memory");

  void *blockers[512];
  unsigned count = exhaust_managed_region(blockers, 512);
  char *q = static_cast<char *>(realloc(p, (1ULL << 30) - 1));
  assert(q && __flexfat_get_base(reinterpret_cast<uintptr_t>(q)) == 0);
  for (unsigned i = 0; i != 175; ++i) {
    assert(q[i] == static_cast<char>(i));
    assert(neighbor[i] == 0);
  }
  for (unsigned i = 0; i != count; ++i)
    free(blockers[i]);
  free(neighbor);
  free(q);
  puts("OK: realloc to system preserves data");
}
// CHECK: OK: realloc to system preserves data
