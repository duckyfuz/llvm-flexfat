// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -fno-builtin -O2 %s -o %t
// RUN: %t free
// RUN: %t realloc
// RUN: not %t free stale 2>&1 | FileCheck %s --check-prefix=STALE
// RUN: not %t realloc stale 2>&1 | FileCheck %s --check-prefix=STALE
// RUN: %clangxx_flexfat_tbi -fsanitize-flexfat-deallocation-check=exact -fno-builtin -O2 %s -o %t
// RUN: not %t free 2>&1 | FileCheck %s --check-prefix=EXACT
// RUN: not %t realloc 2>&1 | FileCheck %s --check-prefix=EXACT
// STALE: FLEXFAT ERROR: temporal violation
// EXACT: FLEXFAT ERROR: invalid deallocation
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
extern "C" void __flexfat_check_temporal(uintptr_t, uintptr_t, int);
int main(int argc, char **argv) {
  char *p = (char *)malloc(64);
  assert(p);
  p[1] = 42;
  char *interior = p + 1;
  asm volatile("" : "+r"(interior) : : "memory");
  if (!strcmp(argv[1], "free")) free(interior);
  else {
    char *q = (char *)realloc(interior, 256);
    assert(q && q[0] == 42);
    free(q);
  }
  if (argc > 2) __flexfat_check_temporal((uintptr_t)p, 1, 0);
  return 0;
}
