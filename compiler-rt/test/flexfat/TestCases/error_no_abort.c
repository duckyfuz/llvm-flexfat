// FlexFat Unit 10: -flexfat-no-abort. An OOB access reports via lowfat_oob_warning
// ("LOWFAT WARNING") and CONTINUES instead of aborting. The overrun is one byte
// past the 16-byte class but within the object's already-committed page, so the
// continued read does not fault; the program runs to completion and exits 0.
//
// RUN: %clang_flexfat -mllvm -flexfat-no-abort -O2 %s -o %t
// RUN: %run %t > %t.out 2>&1
// RUN: FileCheck %s < %t.out
#include <stdlib.h>

__attribute__((noinline)) char get(char *p, int i) { return p[i]; }

int main(int argc, char **argv) {
  char *p = malloc(10);                 // class 16; the page is committed
  volatile char c = get(p, 15 + argc);  // argc=1 -> p[16], OOB but mapped
  (void)c;
  return 0; // execution continues to here -> exit 0
}

// CHECK:      LOWFAT WARNING: out-of-bounds error detected!
// CHECK-NEXT: operation = read
