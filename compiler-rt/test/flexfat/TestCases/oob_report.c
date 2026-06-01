// FlexFat OOB reporter — exact error-text e2e (Unit 5).
//
// Triggers a synthetic heap overflow with FIXED addresses (no ASLR) so the
// LOWFAT ERROR: report is fully deterministic and can be pinned character-for-
// character. The pass-instrumented path arrives in Unit 9; here we link the
// runtime and call lowfat_oob_error directly. stderr is piped (not a TTY), so
// the report is uncolored.
//
// RUN: %clang_flexfat_runtime %s -o %t
// RUN: not --crash %run %t > %t.out 2>&1
// RUN: FileCheck %s < %t.out

#include <lowfat.h>
#include <stdint.h>

int main(void) {
  // base: region 1 (size class 16), offset 0x100 into the heap sub-range.
  void *base = (void *)(((uintptr_t)1 << 35) + 0x100); // 0x800000100
  void *ptr = (void *)((uintptr_t)base + 21);          // 5 past the 16-byte object
  lowfat_oob_error(LOWFAT_OOB_ERROR_READ, ptr, base);
  return 0;
}

// CHECK:      LOWFAT ERROR: out-of-bounds error detected!
// CHECK-NEXT: operation = read
// CHECK-NEXT: pointer   = 0x800000115 (heap)
// CHECK-NEXT: base      = 0x800000100
// CHECK-NEXT: size      = 16
// CHECK-NEXT: overflow  = +5
