// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s

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
  uintptr_t delta = static_cast<uintptr_t>(neighbor - p);
  assert(delta == class_size && "expected adjacent class slots");

  memset(p, 'A', 175);
  memset(neighbor, 0, 175);
  char *neighbor_slot_base = reinterpret_cast<char *>(
      __flexfat_get_base(reinterpret_cast<std::uintptr_t>(neighbor)));
  size_t padding = static_cast<size_t>(neighbor - neighbor_slot_base);
  assert(padding == 0); // LowFat always returns the slot base.

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

  for (size_t i = 0; i < 175; ++i) {
    assert(q[i] == 'A');
    assert(neighbor[i] == 0);
  }
  assert(__flexfat_get_base((uintptr_t)q) == 0);
  // CHECK: OK: realloc to system preserves data
  puts("OK: realloc to system preserves data");

  for (size_t i = 0; i < slot_count; ++i)
    free(blockers[i]);
  free(blockers);
  free(neighbor);
  free(q);
  return 0;
}
