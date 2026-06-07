// FlexFat end-to-end sentinel.
//
// The end-to-end form uses -fsanitize=flexfat (%clang_flexfat), wired into the
// clang driver in Unit 6: this benign program compiles with the flag, links
// libclang_rt.flexfat, runs the FlexFat pass, and executes. The suite's lit
// config gates it to x86_64. (Was XFAIL until the driver flag landed.)
//
// RUN: %clang_flexfat -O0 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s
// CHECK: FlexFat OK

#include <stdio.h>

int main(void) {
  printf("FlexFat OK\n");
  return 0;
}
