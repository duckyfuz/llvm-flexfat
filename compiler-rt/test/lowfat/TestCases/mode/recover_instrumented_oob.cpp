// RUN: %clangxx_lowfat_recover -O0 %s -o %t && %run %t 2>&1 | FileCheck %s

// Recover-mode coverage for compiler-inserted checks, not just memintrinsic
// interceptors. The OOB load should warn and execution should continue.

#include <cstdio>
#include <cstdlib>

volatile char sink;

int main() {
  char *p = (char *)malloc(16);
  if (!p) return 1;

  sink = p[16];

  // CHECK: LOWFAT WARNING: out-of-bounds error detected!
  // CHECK: after instrumented oob
  printf("after instrumented oob\n");
  free(p);
  return 0;
}
