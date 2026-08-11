// RUN: %clangxx_flexfat -O3 -mllvm -flexfat-placement=optimizer-last %s -o %t && %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-MISS
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-CATCH

// More realistic mode-difference test:
// a helper "probes" a header field near the end of a heap buffer, but the
// caller discards the returned value. With the legacy optimizer-last
// placement, the dead return path can be optimized away before the FlexFat pass
// runs. In safe mode, early instrumentation preserves the OOB check even when
// the helper is inlined away later.

#include <cstdio>
#include <cstdlib>
#include <cstdint>

static uint32_t probe_header_magic(char *packet) {
  // Simulate reading a 4-byte header field at byte offset 14. The caller
  // allocated only 16 bytes, so bytes [14, 18) cross the allocation boundary.
  return *reinterpret_cast<uint32_t *>(packet + 14);
}

int main() {
  char *packet = (char *)malloc(16);
  probe_header_magic(packet); // Best-effort probe; caller ignores the result.
  free(packet);

  // CHECK-MISS: processed packet
  // CHECK-CATCH: FLEXFAT ERROR: out-of-bounds error detected!
  printf("processed packet\n");
  return 0;
}
