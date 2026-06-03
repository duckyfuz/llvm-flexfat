// FlexFat Unit 9: a memset whose length overruns the destination must trap with
// the MEMSET (info code 3) report. doset() is noinline so the length is opaque.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t > %t.out 2>&1
// RUN: FileCheck %s < %t.out
#include <stdlib.h>
#include <string.h>

__attribute__((noinline)) void doset(char *d, int c, size_t n) {
  memset(d, c, n);
}

int main(int argc, char **argv) {
  char *dst = malloc(10);             // 16-byte size class
  doset(dst, 0, (size_t)argc * 100);  // 100 bytes, overruns dst
  return dst[0];
}

// CHECK:      LOWFAT ERROR: out-of-bounds error detected!
// CHECK-NEXT: operation = memset
// CHECK-NEXT: pointer   = 0x{{[0-9a-f]+}} (heap)
// CHECK-NEXT: base      = 0x{{[0-9a-f]+}}
// CHECK-NEXT: size      = 16
// CHECK-NEXT: overflow  = +84
