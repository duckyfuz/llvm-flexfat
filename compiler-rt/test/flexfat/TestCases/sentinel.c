// FlexFat end-to-end sentinel (Unit 1).
//
// The end-to-end form uses -fsanitize=flexfat (%clang_flexfat), which is not
// wired into the clang driver until Unit 6. Until then clang rejects the flag,
// so this test is expected to fail. The suite's lit config gates it to x86_64.
//
// When Unit 6 lands the driver flag, this benign program will compile and run,
// the test will start passing -> XPASS -> the suite goes red. That is the signal
// to drop the XFAIL and turn this into a real end-to-end check.
//
// XFAIL: *
//
// RUN: %clang_flexfat -O0 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s
// CHECK: FlexFat OK

#include <stdio.h>

int main(void) {
  printf("FlexFat OK\n");
  return 0;
}
