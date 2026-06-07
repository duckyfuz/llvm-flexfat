// FlexFat Unit 7: an in-bounds program must run clean and exit 0 under
// -fsanitize=flexfat (no false positive on legal heap accesses).
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: %run %t
#include <stdlib.h>

int main(int argc, char **argv) {
  char *p = malloc(64);
  for (int i = 0; i < 64; i++)
    p[i] = (char)(i + argc); // all in bounds
  int sum = 0;
  for (int i = 0; i < 64; i++)
    sum += p[i];
  free(p);
  return sum & 0; // exit 0
}
