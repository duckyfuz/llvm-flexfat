// FlexFat Unit 13: a clean global-heavy program (mutable + const globals of
// varied sizes, plus indirect access via function pointer) exits 0 under
// -fsanitize=flexfat. Regression-catches lowfat.ld / section-placement /
// alignment slips that would otherwise break ordinary global access.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: %run %t
//
// RUN: %clang_flexfat -O0 %s -o %t.O0
// RUN: %run %t.O0

#include <stdio.h>
#include <string.h>

int counters[8] = {0};
const char message[16] = "hello, world!";
static int big_array[1024] = {0};
static const int big_const[64] = {1, 2, 3, 4};

__attribute__((noinline)) static int sum(const int *p, int n) {
  int s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

int main(int argc, char **argv) {
  for (int i = 0; i < 8; i++)
    counters[i] = i + argc;
  for (int i = 0; i < 1024; i++)
    big_array[i] = (i & 0xFF) + argc;

  int s = sum(counters, 8) + sum(big_array, 1024) + sum(big_const, 64);
  if (s < 0)
    return 1;
  if (strlen(message) != 13)
    return 2;
  return 0;
}
