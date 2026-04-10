// RUN: %clang_lowfat %s -o %t && %t
//
// Verify heap layout: allocations of different sizes land in different
// regions and are correctly aligned to their size class.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
uintptr_t __lf_get_base(uintptr_t ptr);
uintptr_t __lf_get_size(uintptr_t ptr);
}

int main() {
  // Allocate objects of various sizes.
  size_t sizes[] = {1, 16, 17, 32, 100, 256, 1024, 4096, 65536};
  size_t expected_classes[] = {16, 16, 32, 32, 128, 256, 1024, 4096, 65536};
  int n = sizeof(sizes) / sizeof(sizes[0]);

  for (int i = 0; i < n; i++) {
    void *p = malloc(sizes[i]);
    if (!p) return 1;

    uintptr_t ptr = (uintptr_t)p;
    uintptr_t base = __lf_get_base(ptr);
    uintptr_t size = __lf_get_size(ptr);

    // Size class must match expected.
    if (size != expected_classes[i]) {
      fprintf(stderr, "FAIL: malloc(%zu) got class %lu, expected %zu\n",
              sizes[i], size, expected_classes[i]);
      return 1;
    }

    // Base must be aligned to its size class.
    if (base % size != 0) {
      fprintf(stderr, "FAIL: base 0x%lx not aligned to size %lu\n", base, size);
      return 1;
    }

    // Pointer must be within [base, base + size).
    if (ptr < base || ptr >= base + size) {
      fprintf(stderr, "FAIL: ptr 0x%lx not in [0x%lx, 0x%lx)\n",
              ptr, base, base + size);
      return 1;
    }

    free(p);
  }

  printf("PASS\n");
  return 0;
}
