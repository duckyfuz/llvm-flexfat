// FlexFat Unit 15: an OOB pointer passed to a callee escapes; the
// pointer-escape check (info code 5 = ESCAPE_CALL) traps before the
// call with `operation = escape (call)`.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <stdlib.h>

// printf is an external function the compiler cannot see through — it
// keeps the call from being optimized away, and the pointer argument
// is the escape site.
int main(void) {
  char *p = malloc(16);
  char *q = p + 100;     // OOB pointer (object is 16 bytes; q is 100 past)
  printf("%p\n", (void *)q); // ESCAPE_CALL — check fires here
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = escape (call)
// CHECK: pointer   = {{.*}} (heap)
// malloc(16) lands in the smallest class >= 17, i.e. class 32.
// CHECK: size      = 32
