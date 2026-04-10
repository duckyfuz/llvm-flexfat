// RUN: %clang_lowfat %s -o %t && %t 2>&1 | FileCheck %s
//
// Verify that accessing within the size class (but past the requested size)
// does not trigger a report, since LowFat checks against the size class, not
// the exact requested size.

#include <stdio.h>
#include <stdlib.h>

int main() {
  // Request 20 bytes — rounds up to 32-byte size class.
  volatile char *p = (volatile char *)malloc(20);
  if (!p) return 1;

  // Access byte 24 — past the 20 requested but within the 32-byte slot.
  // This must NOT trigger an error.
  p[24] = 'A';
  char val = p[24];

  // CHECK-NOT: LOWFAT ERROR
  // CHECK: PASS
  printf("PASS val=%c\n", val);
  free((void *)p);
  return 0;
}
