// RUN: %clang_lowfat %s -o %t && %t
//
// Verify that the LowFat runtime correctly reconstructs base, size, and
// offset from a heap pointer.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
uintptr_t __lf_get_base(uintptr_t ptr);
uintptr_t __lf_get_size(uintptr_t ptr);
uintptr_t __lf_get_offset(uintptr_t ptr);
uintptr_t __lf_get_usable_size(uintptr_t ptr);
}

int main() {
  // Allocate 100 bytes — rounds up to 128-byte size class.
  char *p = (char *)malloc(100);
  if (!p) return 1;

  uintptr_t ptr = (uintptr_t)p;
  uintptr_t base = __lf_get_base(ptr);
  uintptr_t size = __lf_get_size(ptr);
  uintptr_t offset = __lf_get_offset(ptr);
  uintptr_t usable = __lf_get_usable_size(ptr);

  // Base must be <= ptr (ptr is at the start of its slot in normal mode).
  if (base > ptr) {
    fprintf(stderr, "FAIL: base 0x%lx > ptr 0x%lx\n", base, ptr);
    return 1;
  }

  // Size must be a power of 2 and >= 128.
  if (size < 128 || (size & (size - 1)) != 0) {
    fprintf(stderr, "FAIL: size %lu is not a valid POW2 class >= 128\n", size);
    return 1;
  }

  // Offset + usable must equal size.
  if (offset + usable != size) {
    fprintf(stderr, "FAIL: offset(%lu) + usable(%lu) != size(%lu)\n",
            offset, usable, size);
    return 1;
  }

  // Pointer into the middle of the allocation.
  uintptr_t mid = ptr + 50;
  uintptr_t mid_base = __lf_get_base(mid);
  if (mid_base != base) {
    fprintf(stderr, "FAIL: mid base 0x%lx != base 0x%lx\n", mid_base, base);
    return 1;
  }

  free(p);
  printf("PASS\n");
  return 0;
}
