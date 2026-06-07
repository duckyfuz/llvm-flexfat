// FlexFat Unit 13: a global buffer overflow on an eligible array traps
// with pointer = … (global) in the report. clzll(16) = 59, sizes[59] = 32
// (the class-size bump-up — same as for stack/heap), so the runtime
// reports size = 32 for a source 16-byte array.
//
// Caveat (SPEC §II.2): only globals in the main executable are
// protected — the dynamic linker ignores the lowfat sections in shared
// objects. This test puts the global in the main executable.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t 2>&1 | FileCheck %s

char g[16] = {0};

__attribute__((noinline)) static void scribble(char *p, int i) {
  p[i] = 0x41;
}

int main(int argc, char **argv) {
  scribble(g, 32 + argc);  // OOB: 32+1 = 33 > size-class 32
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = write
// CHECK: pointer   = {{.*}} (global)
// CHECK: size      = 32
