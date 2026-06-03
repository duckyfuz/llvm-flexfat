// FlexFat Unit 9: in-bounds memcpy/memset run clean and exit 0 (no false
// positive). Also exercises a constant malloc -> lowfat_malloc_index path.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: %run %t
#include <stdlib.h>
#include <string.h>

__attribute__((noinline)) void docopy(char *d, const char *s, size_t n) {
  memcpy(d, s, n);
}
__attribute__((noinline)) void doset(char *d, int c, size_t n) {
  memset(d, c, n);
}

int main(int argc, char **argv) {
  char *src = malloc(64);
  char *dst = malloc(64); // constant malloc -> lowfat_malloc_index
  doset(src, 1, (size_t)argc * 8);
  docopy(dst, src, (size_t)argc * 8); // 8 bytes, in bounds
  int r = dst[0];
  free(src);
  free(dst);
  return r & 0; // exit 0
}
