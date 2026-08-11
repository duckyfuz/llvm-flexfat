// RUN: %clangxx_flexfat_right_align -O0 %s -o %t && %run %t 2>&1 | FileCheck %s

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" std::uintptr_t __flexfat_get_base(std::uintptr_t ptr);

int main() {
  // 112 bytes maps to the 128-byte class. In right-align mode the returned
  // pointer is shifted 16 bytes into the slot, so copying a full 128 bytes
  // from that pointer incorrectly reads the first 16 bytes of the next slot.
  char *p = static_cast<char *>(malloc(112));
  char *neighbor = static_cast<char *>(malloc(112));
  if (!p || !neighbor)
    return 1;

  uintptr_t delta = static_cast<uintptr_t>(neighbor - p);
  assert(delta == 128 && "expected adjacent 128-byte-class slots");

  memset(p, 'A', 112);
  memset(neighbor, 0, 112);
  char *neighbor_slot_base =
      reinterpret_cast<char *>(__flexfat_get_base(reinterpret_cast<std::uintptr_t>(neighbor)));
  memset(neighbor_slot_base, 0x5A, 16);

  // Force the FlexFat -> system-malloc migration path in realloc().
  constexpr size_t HugeSize = (1ULL << 30) + 1;
  char *q = static_cast<char *>(realloc(p, HugeSize));
  if (!q) {
    // Extremely unlikely on the supported 64-bit targets because the system
    // allocator usually overcommits here, but avoid a spurious hard failure.
    free(neighbor);
    puts("SKIP");
    return 0;
  }

  bool copied_neighbor_prefix = true;
  for (size_t i = 112; i < 128; ++i) {
    if (static_cast<unsigned char>(q[i]) != 0x5A) {
      copied_neighbor_prefix = false;
      break;
    }
  }

  // CHECK: OK: neighbor prefix not copied
  // CHECK-NOT: BUG: copied neighbor prefix
  if (copied_neighbor_prefix)
    puts("BUG: copied neighbor prefix");
  else
    puts("OK: neighbor prefix not copied");

  free(neighbor);
  free(q);
  return 0;
}
