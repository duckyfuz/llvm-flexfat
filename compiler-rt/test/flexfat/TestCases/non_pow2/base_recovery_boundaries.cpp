// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t
// RUN: %clangxx_flexfat -O1 %s -o %t && %run %t
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t
// REQUIRES: flexfat-custom-config

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);

__attribute__((noinline)) static void escape(char *p) {
  asm volatile("" : : "r"(p) : "memory");
}

int main() {
  void *allocation = malloc(47);
  assert(allocation != nullptr);

  const uintptr_t base = __flexfat_get_base((uintptr_t)allocation);
  const uintptr_t size = __flexfat_get_size((uintptr_t)allocation);
  assert(size == 48);
  assert(__flexfat_get_base(base) == base);
  assert(__flexfat_get_base(base + size - 1) == base);
  assert(__flexfat_get_base(base + size) == base + size);

  // The final slot byte is reserved; its address is a valid one-past escape.
  volatile char *payload = (volatile char *)allocation;
  payload[size - 2] = 42;
  assert(payload[size - 2] == 42);
  escape((char *)allocation + size - 1);

  free(allocation);
  return 0;
}
