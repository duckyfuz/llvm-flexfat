// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat_right_align -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// Mode-difference test: one-past-end overflow on an allocation where aligned
// right-biasing still leaves a non-zero shift within the slot.
//
// Default (left-align): a 176-byte object lives at the start of a larger
// slot; buf[176] falls in the right padding and is not caught.
//
// Right-align: the same object is shifted by at least 16 bytes while preserving
// malloc alignment; buf[176] reaches the slot boundary -> OOB -> caught.

#include <cstdio>
#include <cstdlib>

int main() {
  // 176 leaves an exact multiple of 16 bytes of slack in both profiles.
  char *buf = (char *)malloc(176);
  if (!buf)
    return 1;

  buf[176] = 'X'; // one-past-end write

  // CHECK-MISS: overflow: not caught (in right padding)
  // CHECK-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
  printf("overflow: not caught (in right padding)\n");
  free(buf);
  return 0;
}
