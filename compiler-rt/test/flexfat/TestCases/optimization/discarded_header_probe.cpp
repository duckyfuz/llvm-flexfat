// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O3 %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat -mllvm -flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// A discarded read is checked at O0, but can be removed by optimization
// before FlexFat instruments at OptimizerLastEP. Whole-access checking is
// enabled in both runs so the difference comes from optimization timing.

#include <cstdio>
#include <cstdlib>
#include <cstdint>

static uint32_t probe_header_magic(char *packet) {
  // Simulate reading a 4-byte header field at byte offset 14. The caller
  // allocated 15 bytes in a 16-byte slot, so bytes [14, 18) cross the allocation boundary.
  return *reinterpret_cast<uint32_t *>(packet + 14);
}

int main() {
  char *packet = (char *)malloc(15);
  probe_header_magic(packet); // Best-effort probe; caller ignores the result.
  free(packet);

  // CHECK-MISS: processed packet
  // CHECK-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
  printf("processed packet\n");
  return 0;
}
