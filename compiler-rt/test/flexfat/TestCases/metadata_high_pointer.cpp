// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t

#include <assert.h>
#include <stdint.h>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);

int main() {
  const uintptr_t high = UINT64_C(0xffff000000001234);
  assert(__flexfat_get_base(high) == 0);
  assert(__flexfat_get_size(high) == UINTPTR_MAX);
  return 0;
}
