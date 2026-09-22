// RUN: %clangxx_flexfat -O0 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O3 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_safe -O0 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_safe -O2 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_safe -O3 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O0 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O2 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O3 %s -o %t
// RUN: not %run %t read 2>&1 | FileCheck %s
// RUN: not %run %t write 2>&1 | FileCheck %s
// RUN: not %run %t copy 2>&1 | FileCheck %s
// RUN: not %run %t set 2>&1 | FileCheck %s

#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);
static char *volatile Cursor;
static volatile char Sink;

int main(int Argc, char **Argv) {
  volatile size_t Request = 23;
  char *First = static_cast<char *>(malloc(Request));
  char *Second = static_cast<char *>(malloc(Request));
  assert(First && Second && Argc == 2);
  uintptr_t Base = __flexfat_get_base(reinterpret_cast<uintptr_t>(First));
  size_t Slot = __flexfat_get_size(reinterpret_cast<uintptr_t>(First));
  assert(__flexfat_get_base(reinterpret_cast<uintptr_t>(Second)) ==
         Base + Slot);
  Cursor = First;
  void *(*volatile Copy)(void *, const void *, size_t) = memcpy;
  void *(*volatile Set)(void *, int, size_t) = memset;
  char Byte = 0;
  // Each reload loses the original companion base. The store at the exact
  // slot boundary must report before recovery can switch to Second's slot.
  for (size_t I = 0; I != 2 * Slot; ++I) {
    char *P = Cursor;
    switch (Argv[1][0]) {
    case 'r':
      Sink = *reinterpret_cast<volatile char *>(P);
      break;
    case 'w':
      *reinterpret_cast<volatile char *>(P) = 1;
      break;
    case 'c':
      Copy(&Byte, P, 1);
      break;
    case 's':
      Set(P, 0, 1);
      break;
    }
    Cursor = P + 1;
  }
  free(Second);
  free(First);
}
// CHECK: FLEXFAT ERROR: out-of-bounds error detected!
