// RUN: %clangxx_flexfat -O0 %s -o %t
// RUN: %run %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t range 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O3 %s -o %t
// RUN: %run %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t range 2>&1 | FileCheck %s
// CHECK: FLEXFAT ERROR: out-of-bounds error detected!

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>

extern "C" uintptr_t __flexfat_get_size(uintptr_t);
extern "C" uintptr_t __flexfat_get_usable_size(uintptr_t);
extern "C" void *__interceptor_memset(void *, int, size_t);

__attribute__((noinline)) static char *opaque(char *p) {
  asm volatile("" : "+r"(p) : : "memory");
  return p;
}

int main(int argc, char **argv) {
  char *p = opaque(static_cast<char *>(malloc(15)));
  assert(p);
  assert(__flexfat_get_size((uintptr_t)p) == 16);
  assert(__flexfat_get_usable_size((uintptr_t)p) == 15);
  assert(malloc_usable_size(p) == 15);
  memset(p, 90, 15);
  char *end = opaque(p + 15);
  if (argc > 1 && !strcmp(argv[1], "read"))
    return *(volatile unsigned char *)end;
  if (argc > 1 && !strcmp(argv[1], "write"))
    *(volatile unsigned char *)end = 1;
  if (argc > 1 && !strcmp(argv[1], "range"))
    __interceptor_memset(p, 0, 16);
  assert(*(volatile unsigned char *)opaque(p + 14) == 90);
  free(p);
}
