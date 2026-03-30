// RUN: %clangxx_lowfat -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s

#include <cstdio>
#include <cstdlib>
#include <cstdint>

int main() {
  char *p = (char *)malloc(16);
  if (!p) return 1;

  uintptr_t raw = (uintptr_t)p + 16;
  int *q = (int *)raw;
  int expected = 0;
  __atomic_compare_exchange_n(q, &expected, 1, false,
                              __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);

  // CHECK: LOWFAT ERROR: out-of-bounds error detected!
  // CHECK: operation = write
  printf("DONE\n");
  free(p);
  return 0;
}
