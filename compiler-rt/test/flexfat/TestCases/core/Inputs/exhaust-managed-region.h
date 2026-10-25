#include <cassert>
#include <cstdint>
#include <cstdlib>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);

// Keep managed slots live until the next allocation falls back to the system.
static unsigned exhaust_managed_region(void **slots, unsigned capacity) {
  for (unsigned i = 0; i != capacity; ++i) {
    slots[i] = malloc((1ULL << 30) - 1);
    asm volatile("" : : "r"(slots[i]) : "memory");
    assert(slots[i]);
    if (__flexfat_get_base(reinterpret_cast<uintptr_t>(slots[i])) == 0) {
      assert(i != 0 && "expected managed allocations before exhaustion");
      return i + 1;
    }
  }
  assert(false && "managed region did not exhaust within fixture capacity");
  return 0;
}
