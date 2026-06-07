// FlexFat Unit 17: POW2 end-to-end smoke test. Allocate, write at the last
// in-bounds byte (must succeed), write one past the class boundary (must
// trap with `operation = write`, `size = 64`, the same byte-identical OOB-
// report format the reference uses).
//
// POW2 size-class bump-up matters here. POW2 classes are 16/32/64/128/...;
// heap_select returns the smallest class C such that C >= K+1. So a literal
// malloc(64) lands in class 128 (size 64+1 needs C>=65, so C=128) and the
// class boundary is 128, not 64 -- p[64] would be in-bounds. To trap at p[64]
// we need malloc(K) with K in (32, 64], so heap_select returns class 3
// (size 64). Pick malloc(63) -- class 64, last in-bounds byte is p[63],
// p[64] is one past the end of the class.
//
// Closes the Unit 16 audit finding: POW2 parity was previously established
// at the config+encoding layer only (Unit 2 byte-diff of generator output;
// Unit 3 gtest analytic Pow2BaseFormula). This is the FIRST test that
// actually builds and runs a POW2 binary end-to-end -- pass POW2 codegen
// (single `and` for lowfat_base), POW2 runtime allocator dispatch, POW2
// region layout, all under one cover.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t 2>&1 | FileCheck %s
//
// REQUIRES: x86_64-target-arch
// REQUIRES: flexfat-pow2

#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) static void scribble(char *p, int i) {
  p[i] = (char)(i + 1);
}

int main(void) {
  // POW2 heap_select(63) -> class 64 (idx 3). lowfat_size(p) at runtime = 64.
  char *p = (char *)malloc(63);
  if (!p) return 1;
  // In-bounds writes succeed (the last byte at the class boundary, offset 63).
  scribble(p, 63);
  // One past the class boundary traps with operation=write, size=64.
  scribble(p, 64);
  free(p);
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = write
// CHECK: pointer   = {{.*}} (heap)
// CHECK: size      = 64
