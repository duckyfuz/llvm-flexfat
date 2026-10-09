// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat_right_align -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// Mode-difference test: overflow beyond the reserved byte where aligned
// right-biasing still leaves a non-zero shift within the slot.
//
// Default (left-align): a 175-byte object lives at the start of a larger
// slot; buf[176] falls in the right padding and is not caught.
//
// Right-align: the same object is shifted by at least 16 bytes while preserving
// malloc alignment and reserving one trailing byte; buf[176] reaches the slot boundary -> OOB -> caught.

#include <cstdio>
#include <cstdlib>

int main() {
  // 175 plus the reserved byte leaves alignment-compatible slack.
  char *buf = (char *)malloc(175);
  if (!buf)
    return 1;

  buf[176] = 'X'; // Cross the reserved trailing padding too.

  // CHECK-MISS: overflow: not caught (in right padding)
  // CHECK-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
  printf("overflow: not caught (in right padding)\n");
  free(buf);
  return 0;
}
