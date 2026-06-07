// FlexFat Unit 15: returning an OOB pointer escapes it; the
// pointer-escape check (info code 6 = ESCAPE_RETURN) traps before
// the ret with `operation = escape (return)`.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <stdlib.h>

__attribute__((noinline)) static char *make_oob_ptr(void) {
  char *p = malloc(16);
  return p + 100;          // ESCAPE_RETURN — check fires at this ret
}

int main(void) {
  // printf forces the optimizer to keep the returned pointer live, so
  // make_oob_ptr's body isn't dead-stripped.
  printf("%p\n", (void *)make_oob_ptr());
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = escape (return)
// CHECK: pointer   = {{.*}} (heap)
// CHECK: size      = 32
