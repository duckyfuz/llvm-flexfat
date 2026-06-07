// FlexFat Unit 7: a heap out-of-bounds read must trap with the LOWFAT ERROR
// report (the SPEC §1.4 / README heap example). Built with -fsanitize=flexfat,
// malloc is interposed to the lowfat allocator and the instrumented load in
// get() is bounds-checked. get() is noinline so q stays an opaque (fat) pointer
// argument -- the optimizer can't prove its bounds, so the access survives to
// -O2 exactly as in the README. Addresses are ASLR-random, so only the
// deterministic fields (operation, size class, overflow, heap kind) are pinned.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t > %t.out 2>&1
// RUN: FileCheck %s < %t.out
#include <stdlib.h>

__attribute__((noinline)) char get(char *q, int i) { return q[i]; }

int main(int argc, char **argv) {
  char *p = malloc(10);    // rounded up to the 16-byte size class (region 1)
  return get(p, argc * 100); // argc>=1 -> read p+100, out of bounds
}

// CHECK:      LOWFAT ERROR: out-of-bounds error detected!
// CHECK-NEXT: operation = read
// CHECK-NEXT: pointer   = 0x{{[0-9a-f]+}} (heap)
// CHECK-NEXT: base      = 0x{{[0-9a-f]+}}
// CHECK-NEXT: size      = 16
// CHECK-NEXT: overflow  = +84
