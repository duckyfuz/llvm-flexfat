// FlexFat Unit 12a: a stack-heavy program (large locals, recursion, args/env
// access) must run cleanly under the runtime's pivoted stack and exit 0. This
// regression-catches obvious pivot bugs: stack-copy off-by-ones, missing
// self-referential-pointer patches, alignment slips. No alloca pass change in
// 12a so behavior is identical to native modulo the pivot itself.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: %run %t
//
// RUN: %clang_flexfat -O0 %s -o %t.O0
// RUN: %run %t.O0

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sum_args(int argc, char **argv) {
  int s = 0;
  for (int i = 0; i < argc; i++)
    s += (int)strlen(argv[i]);
  return s;
}

static int recurse(int n, int acc) {
  if (n == 0)
    return acc;
  char buf[128];
  memset(buf, (char)(n & 0xFF), sizeof(buf));
  // Force the compiler to keep `buf` alive across the call.
  int x = (unsigned char)buf[n % 128];
  return recurse(n - 1, acc + x);
}

int main(int argc, char **argv) {
  int s = sum_args(argc, argv);
  int r = recurse(1000, s);
  if (r < 0)
    return 1;

  char large[8192];
  memset(large, 0xAB, sizeof(large));
  for (size_t i = 0; i < sizeof(large); i++)
    if (large[i] != (char)0xAB)
      return 2;

  printf("ok r=%d argc=%d\n", r, argc);
  fflush(stdout);
  return 0;
}
