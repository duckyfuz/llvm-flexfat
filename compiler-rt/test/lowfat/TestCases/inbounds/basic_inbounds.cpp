// RUN: %clang_lowfat %s -o %t && %t
//
// Verify that in-bounds accesses do not trigger false positives.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main() {
  // Test various sizes.
  int sizes[] = {1, 15, 16, 31, 32, 64, 128, 255, 256, 1000, 4096};
  int n = sizeof(sizes) / sizeof(sizes[0]);

  for (int i = 0; i < n; i++) {
    char *p = (char *)malloc(sizes[i]);
    if (!p) return 1;

    // Write every byte within the allocation.
    memset(p, 0x42, sizes[i]);

    // Read every byte.
    for (int j = 0; j < sizes[i]; j++) {
      if (p[j] != 0x42) {
        fprintf(stderr, "FAIL: readback mismatch at offset %d\n", j);
        return 1;
      }
    }

    free(p);
  }

  printf("PASS\n");
  return 0;
}
