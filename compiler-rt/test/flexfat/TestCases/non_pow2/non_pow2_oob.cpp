// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -O1 %s -o %t && not %run %t 2>&1 | FileCheck %s

// REQUIRES: flexfat-custom-config

#include <cstdlib>
#include <cassert>
#include <cstdint>
extern "C" uintptr_t __flexfat_get_size(uintptr_t);
struct __attribute__((packed)) UnalignedDouble { double value; };

int main() {
  char *p = (char *)malloc(47);
  if (!p) return 1;

  assert(__flexfat_get_size((uintptr_t)p) == 48);
  volatile UnalignedDouble *val = (volatile UnalignedDouble *)(p + 44);
  val->value = 1.0;

  free(p);
  return 0;
}

// CHECK: FLEXFAT ERROR: out-of-bounds error detected!
