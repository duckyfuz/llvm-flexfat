// FlexFat Unit 10: -flexfat-signal. An OOB access traps via an inline `ud2`
// (SIGILL) with NO runtime call -- so the program dies with SIGILL (132 =
// 128+4), not SIGABRT (134), and prints no LOWFAT report.
//
// RUN: %clang_flexfat -mllvm -flexfat-signal -O2 %s -o %t
// RUN: not --crash %run %t > %t.out 2>&1
// RUN: FileCheck %s --allow-empty < %t.out
// Pin the exact signal: SIGILL (132 = 128+4), not SIGABRT (134). lit's internal
// shell does not expand $?, so use a real shell.
// RUN: sh -c '%run %t; test $? -eq 132'
#include <stdlib.h>

__attribute__((noinline)) char get(char *p, int i) { return p[i]; }

int main(int argc, char **argv) {
  char *p = malloc(10);
  return get(p, argc * 100); // OOB -> ud2/SIGILL before the access; no report
}

// No runtime call on the trap path, so no report is printed:
// CHECK-NOT: LOWFAT
