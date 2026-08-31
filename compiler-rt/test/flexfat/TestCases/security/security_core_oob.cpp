// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-READ
// RUN: %clangxx_flexfat -O0 %s -DWRITE -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-WRITE
// RUN: %clangxx_flexfat -O0 %s -DUNDERFLOW -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-UNDERFLOW
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-READ
// RUN: %clangxx_flexfat -O2 %s -DWRITE -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-WRITE
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-READ
// RUN: %clangxx_flexfat -O3 %s -DWRITE -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-WRITE

#include <stdlib.h>
#include <stdio.h>

__attribute__((noinline)) static size_t opaqueSize(size_t size) {
  asm volatile("" : "+r"(size));
  return size;
}

__attribute__((noinline)) static long opaqueOffset(long offset) {
  asm volatile("" : "+r"(offset));
  return offset;
}

int main() {
  volatile size_t runtime_size = 16;
  size_t size = runtime_size;
  char *p = (char *)malloc(size);
  if (!p) return 1;

#ifdef UNDERFLOW
  // CHECK-UNDERFLOW: FLEXFAT ERROR: out-of-bounds error detected!
  // CHECK-UNDERFLOW: operation = read
  volatile char c =
      *reinterpret_cast<volatile char *>(p + opaqueOffset(-1));
  (void)c;
#elif defined(WRITE)
  // CHECK-WRITE: FLEXFAT ERROR: out-of-bounds error detected!
  // The store itself is checked using the originating allocation.
  // CHECK-WRITE: operation = write
  *reinterpret_cast<volatile char *>(p + opaqueSize(size)) = 'x';
#else
  // CHECK-READ: FLEXFAT ERROR: out-of-bounds error detected!
  // CHECK-READ: operation = read
  volatile char c =
      *reinterpret_cast<volatile char *>(p + opaqueSize(size));
  (void)c;
#endif

  free(p);
  return 0;
}
