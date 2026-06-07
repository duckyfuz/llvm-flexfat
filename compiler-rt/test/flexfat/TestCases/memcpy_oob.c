// FlexFat Unit 9: a memcpy whose length overruns the destination must trap with
// the MEMCPY (info code 2) report. docopy() is noinline so the length is opaque
// (not constant-folded / not provably in-bounds), exercising the pass-emitted
// end-pointer check that resolves against the real runtime.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t > %t.out 2>&1
// RUN: FileCheck %s < %t.out
#include <stdlib.h>
#include <string.h>

__attribute__((noinline)) void docopy(char *d, const char *s, size_t n) {
  memcpy(d, s, n);
}

int main(int argc, char **argv) {
  char *src = malloc(256);             // big enough (no source overrun)
  char *dst = malloc(10);              // rounded up to the 16-byte size class
  docopy(dst, src, (size_t)argc * 100); // argc>=1 -> 100 bytes, overruns dst
  return dst[0];
}

// CHECK:      LOWFAT ERROR: out-of-bounds error detected!
// CHECK-NEXT: operation = memcpy
// CHECK-NEXT: pointer   = 0x{{[0-9a-f]+}} (heap)
// CHECK-NEXT: base      = 0x{{[0-9a-f]+}}
// CHECK-NEXT: size      = 16
// CHECK-NEXT: overflow  = +84
