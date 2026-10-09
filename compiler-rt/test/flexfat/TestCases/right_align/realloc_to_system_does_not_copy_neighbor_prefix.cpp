// RUN: %clangxx_flexfat_right_align -mllvm -flexfat-mode=safe -O2 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O2 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O3 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O0 %s -o %t && %run %t 2>&1 | FileCheck %s

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" std::uintptr_t __flexfat_get_base(std::uintptr_t ptr);
extern "C" std::uintptr_t __flexfat_get_size(std::uintptr_t ptr);

int main() {
  char *p = static_cast<char *>(malloc(175));
  char *neighbor = static_cast<char *>(malloc(175));
  if (!p || !neighbor)
    return 1;

  size_t class_size = __flexfat_get_size(reinterpret_cast<uintptr_t>(p));
  size_t old_usable =
      class_size - (reinterpret_cast<uintptr_t>(p) -
                    __flexfat_get_base(reinterpret_cast<uintptr_t>(p)));
  uintptr_t delta = static_cast<uintptr_t>(neighbor - p);
  assert(delta == class_size && "expected adjacent class slots");

  memset(p, 'A', 175);
  memset(neighbor, 0, 175);
  char *neighbor_slot_base = reinterpret_cast<char *>(
      __flexfat_get_base(reinterpret_cast<std::uintptr_t>(neighbor)));
  size_t padding = static_cast<size_t>(neighbor - neighbor_slot_base);
  assert(padding != 0);
  memset(neighbor_slot_base, 0x5A, padding);

  // Exhaust the 1 GiB class, then force matched system fallback in realloc.
  size_t slot_count = class_size == 192 ? 256 : 4;
  void **blockers = static_cast<void **>(calloc(slot_count, sizeof(void *)));
  for (size_t i = 0; i < slot_count; ++i) {
    blockers[i] = malloc((1ULL << 30) - 1);
    asm volatile("" : : "r"(blockers[i]) : "memory");
    assert(blockers[i]);
  }
  char *q = static_cast<char *>(realloc(p, (1ULL << 30) - 1));
  if (!q) {
    // Extremely unlikely on the supported 64-bit targets because the system
    // allocator usually overcommits here, but avoid a spurious hard failure.
    for (size_t i = 0; i < slot_count; ++i)
      free(blockers[i]);
    free(blockers);
    free(neighbor);
    puts("SKIP");
    return 0;
  }

  bool copied_neighbor_prefix = true;
  for (size_t i = old_usable; i < old_usable + padding; ++i) {
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

  for (size_t i = 0; i < slot_count; ++i)
    free(blockers[i]);
  free(blockers);
  free(neighbor);
  free(q);
  return 0;
}
