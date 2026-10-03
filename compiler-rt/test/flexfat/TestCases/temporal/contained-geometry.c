// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -O2 -fno-builtin %s -o %t && %t
// RUN: not %t stale 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_tbi -O2 -fno-builtin -fsanitize-flexfat-tbi-storage=prior-byte %s -o %t.prior && %t.prior
// RUN: not %t.prior stale 2>&1 | FileCheck %s
// CHECK: operation = read
// CHECK: reason = generation mismatch

#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  volatile int *p = (volatile int *)malloc(64);
  if (!p)
    return 1;
  p[1] = 7;
  p[2] = 11;
  int sum = p[1] + p[2];
  free((void *)p);
  if (argc > 1 && strcmp(argv[1], "stale") == 0)
    return p[2];
  return sum != 18;
}
