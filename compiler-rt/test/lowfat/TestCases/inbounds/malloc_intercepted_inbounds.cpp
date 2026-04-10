// RUN: %clang_lowfat %s -o %t && %t
//
// Verify that malloc interception routes through LowFat and that the
// resulting pointer is bounds-checkable.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" uintptr_t __lf_get_size(uintptr_t ptr);

int main() {
  // malloc should be intercepted and return a LowFat pointer.
  void *p = malloc(48);
  if (!p) return 1;

  uintptr_t size = __lf_get_size((uintptr_t)p);

  // 48 rounds up to 64-byte size class.
  if (size != 64) {
    fprintf(stderr, "FAIL: expected class 64, got %lu\n", size);
    return 1;
  }

  // In-bounds write at the last byte of the actual allocation.
  volatile char *vp = (volatile char *)p;
  vp[47] = 'Z';

  free(p);
  printf("PASS\n");
  return 0;
}
