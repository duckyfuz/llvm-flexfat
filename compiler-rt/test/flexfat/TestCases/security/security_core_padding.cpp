// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

int main() {
  volatile char *p = (volatile char *)malloc(17);
  if (!p) return 1;

  p[0] = 'a';
  p[16] = 'b';

  p[17] = 'c';
  p[30] = 'z';

  printf("Padding access: ok\n");

  if (p[0] != 'a' || p[16] != 'b' || p[17] != 'c' || p[30] != 'z')
    return 2;
  free((void *)p);
  return 0;
}

// CHECK: Padding access: ok
// CHECK-NOT: FLEXFAT ERROR
