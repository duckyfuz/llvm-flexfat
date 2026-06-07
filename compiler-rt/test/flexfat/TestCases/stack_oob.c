// FlexFat Unit 12b: a stack buffer overflow on an *escaping* local must trap
// with `pointer = … (stack)` in the report. The escape is via a noinline call
// (the callee can observe the address, so doesAllocaEscape returns true and
// makeAllocaLowFatPtr mirrors the alloca into a size-class region — the
// runtime bounds check then catches the OOB write).
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t 2>&1 | FileCheck %s
//
// REQUIRES: x86_64-target-arch
#include <stdio.h>

__attribute__((noinline)) static void scribble(char *p, int i) {
  // The callee sees an opaque pointer to the local; the compiler cannot prove
  // i is in-bounds, so this becomes a runtime-checked store.
  p[i] = 0x41;
}

int main(int argc, char **argv) {
  char buf[16];
  scribble(buf, 32 + argc);   // OOB: i = 32+1 = 33 >> sizeof buf
  printf("did not trap\n");
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = write
// CHECK: pointer   = {{.*}} (stack)
// Class-size bump-up: clzll(16) = 59, sizes[59] = 32 (the next class so the
// one-past-end byte falls within the same allocation slot), so the runtime
// reports the class size 32 — not the source size 16.
// CHECK: size      = 32
