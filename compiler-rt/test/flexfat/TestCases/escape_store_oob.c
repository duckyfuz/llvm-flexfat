// FlexFat Unit 15: storing an OOB pointer into memory escapes it; the
// pointer-escape check (info code 7 = ESCAPE_STORE) traps before the
// store with `operation = escape (store)`.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: not --crash %run %t 2>&1 | FileCheck %s

#include <stdio.h>
#include <stdlib.h>

// Global slot the store escapes into. printf at the end forces the
// optimizer to keep the store live (otherwise sink_slot is unread).
static char **sink_slot;

int main(void) {
  sink_slot = (char **)malloc(sizeof(char *));
  char *p = malloc(16);
  char *q = p + 100;       // OOB
  *sink_slot = q;          // ESCAPE_STORE — check fires here
  printf("%p\n", (void *)*sink_slot);
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = escape (store)
// CHECK: pointer   = {{.*}} (heap)
// CHECK: size      = 32
