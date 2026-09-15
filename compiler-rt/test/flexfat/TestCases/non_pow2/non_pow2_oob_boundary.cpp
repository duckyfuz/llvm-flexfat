// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat_safe -O1 %s -o %t && %run %t | FileCheck %s

// Exact one-past escape is valid regardless of optimization or pass placement.
//
// REQUIRES: flexfat-custom-config

#include <stdio.h>
#include <stdlib.h>

// noinline keeps this as a cross-function pointer-escape case.
__attribute__((noinline))
static void sink(volatile char *q) {
  asm volatile("" : : "r"(q) : "memory");
  printf("%p\n", (void *)q);
}

int main() {
  char *p = (char *)malloc(48);
  if (!p) return 1;

  sink(p + 48);

  free(p);
  return 0;
}

// CHECK: 0x
