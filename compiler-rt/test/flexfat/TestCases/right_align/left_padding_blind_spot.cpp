// RUN: %clangxx_flexfat_right_align -O0 %s -o %t && %run %t 2>&1 | FileCheck %s

// Documents the known trade-off of right-align mode: underflows into the left
// padding are not caught because the shifted pointer still falls within the
// same slot.
//
// A 176-byte object has at least 16 bytes of aligned left padding in both
// layouts, so buf[-1] remains inside the class slot.
//
// buf[-1] = slot_base+15, which is inside the slot:
//   GetBase(slot_base+15) = slot_base
//   (slot_base+15 - slot_base) = 15 < 128 -> NOT OOB

#include <cstdio>
#include <cstdlib>

int main() {
  char *buf = (char *)malloc(176);
  if (!buf)
    return 1;

  // Write one byte into the left padding (blind spot).
  // This is technically out-of-bounds for the requested allocation, but
  // right-align mode cannot detect it because the access stays within the same slot.
  buf[-1] = 'X';

  // CHECK: blind spot: not caught (left padding)
  // CHECK-NOT: FLEXFAT ERROR
  printf("blind spot: not caught (left padding)\n");
  free(buf);
  return 0;
}
