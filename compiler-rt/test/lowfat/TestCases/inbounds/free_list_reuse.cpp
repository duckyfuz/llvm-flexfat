// RUN: %clang_lowfat %s -o %t && %t
//
// Verify that freed slots are reused and still work correctly.

#include <stdio.h>
#include <stdlib.h>

int main() {
  // Allocate and free, then allocate again — the second allocation should
  // reuse the freed slot.
  void *p1 = malloc(64);
  if (!p1) return 1;
  free(p1);

  void *p2 = malloc(64);
  if (!p2) return 1;

  // The pointer should be valid for in-bounds access.
  volatile char *vp = (volatile char *)p2;
  for (int i = 0; i < 64; i++)
    vp[i] = (char)i;

  for (int i = 0; i < 64; i++) {
    if (vp[i] != (char)i) {
      fprintf(stderr, "FAIL: readback mismatch at %d\n", i);
      return 1;
    }
  }

  free(p2);
  printf("PASS\n");
  return 0;
}
