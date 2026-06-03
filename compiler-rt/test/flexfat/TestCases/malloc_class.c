// FlexFat: behavioral size-table drift guard. A constant malloc(K) is folded by
// the pass to lowfat_malloc_index(idx, K) with idx computed host-side from
// FlexFatSizes.inc. This closes the loop end-to-end: the object must land in the
// SAME region / size class the runtime allocator uses. malloc(100) -> region 7,
// size class 112 (the 16..112 classes, one-past-end rounding). The last in-bounds
// byte p[111] must pass; one-past p[112] must trap with size = 112.
//
// If the pass and runtime size tables ever drift, the object lands in a different
// class: either the in-bounds run traps (false positive) or the OOB run fails to
// trap (missed bug) -- so this test fails on drift in either direction.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: %run %t
// RUN: not --crash %run %t x > %t.out 2>&1
// RUN: FileCheck %s < %t.out
#include <stdlib.h>

__attribute__((noinline)) char get(char *p, int i) { return p[i]; }

int main(int argc, char **argv) {
  char *p = malloc(100);         // -> lowfat_malloc_index(7, 100), size class 112
  return get(p, 110 + argc) & 0; // argc=1: p[111] in bounds; argc=2: p[112] OOB
}

// CHECK:      LOWFAT ERROR: out-of-bounds error detected!
// CHECK-NEXT: operation = read
// CHECK-NEXT: pointer   = 0x{{[0-9a-f]+}} (heap)
// CHECK-NEXT: base      = 0x{{[0-9a-f]+}}
// CHECK-NEXT: size      = 112
