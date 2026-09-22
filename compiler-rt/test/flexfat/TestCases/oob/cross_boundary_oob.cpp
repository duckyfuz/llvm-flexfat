// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O1 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s

// Cross-boundary OOB write must be reported.

extern "C" void *__flexfat_malloc(unsigned long size);

int main() {
  // Request 15 bytes plus the reserved trailing byte in a 16-byte slot.
  char *buf = (char *)__flexfat_malloc(15);
  if (!buf)
    return 1;

  // In-bounds write
  buf[0] = 'A';
  buf[14] = 'B';

  // Cross-boundary OOB: write 8 bytes at offset 12
  // ptr + 8 = buf+20 > buf+16: out of bounds.
  // CHECK: FLEXFAT ERROR: out-of-bounds error detected!
  double *cross = (double *)(buf + 12);
  *cross = 3.14;

  return 0;
}
