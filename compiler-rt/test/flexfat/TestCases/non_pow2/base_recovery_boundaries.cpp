// REQUIRES: flexfat-custom-config
// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);

int main() {
  void *allocation = malloc(48);
  assert(allocation != nullptr);

  const uintptr_t base = __flexfat_get_base((uintptr_t)allocation);
  const uintptr_t size = __flexfat_get_size((uintptr_t)allocation);
  assert(size == 48);
  assert(__flexfat_get_base(base) == base);
  assert(__flexfat_get_base(base + size - 1) == base);
  assert(__flexfat_get_base(base + size) == base + size);

  free(allocation);
  return 0;
}
