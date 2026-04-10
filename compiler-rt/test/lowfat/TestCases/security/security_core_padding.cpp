// RUN: %clang_lowfat %s -o %t && %t
//
// Verify that accessing the padding between requested size and size class
// does NOT trigger a false positive (LowFat checks class boundaries, not
// exact allocation size).

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" uintptr_t __lf_get_size(uintptr_t ptr);

int main() {
  // Request 20 bytes, which rounds up to 32.
  volatile char *p = (volatile char *)malloc(20);
  if (!p) return 1;

  uintptr_t size = __lf_get_size((uintptr_t)p);
  if (size != 32) {
    fprintf(stderr, "FAIL: expected class 32, got %lu\n", size);
    return 1;
  }

  // Access bytes 20-31 (padding). Should be fine.
  for (int i = 20; i < 32; i++)
    p[i] = (char)i;

  for (int i = 20; i < 32; i++) {
    if (p[i] != (char)i) {
      fprintf(stderr, "FAIL: padding readback mismatch at %d\n", i);
      return 1;
    }
  }

  free((void *)p);
  printf("PASS\n");
  return 0;
}
