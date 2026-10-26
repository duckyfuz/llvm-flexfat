// REQUIRES: flexfat-tbi
// UNSUPPORTED: flexfat-custom-config
// RUN: %clang -O2 %flexfat_config_flags -c %s -o %t.o
// RUN: %clangxx_flexfat_tbi -fsanitize-flexfat-tbi=shadow %t.o -o %t && %run %t

#include "flexfat/flexfat_config.h"
#include "flexfat/flexfat_interface.h"
#include <assert.h>
#include <stdint.h>
using namespace __flexfat;

static void check(uptr r, uptr raw) {
  const uptr size = SizeClassToSize(r);
  const uptr index = raw >> kRegionSizeLog;
  const auto *sizes = reinterpret_cast<const uptr *>(kTablesBase);
  assert(sizes[index] == size);
  uptr q = raw >> (r + 4);
  assert(q == raw / size);
  uptr start = GetRegionStart(r), end = start + kRegionSize;
  const uptr shadow_offset = 0x200000000000ULL;
  uptr base = raw & ~(size - 1);
  uptr address = shadow_offset + (base >> kMinSizeLog);
  assert(address >= shadow_offset + (start >> kMinSizeLog));
  assert(address < shadow_offset + (end >> kMinSizeLog));
  if (q * size < start || q * size > end - size || raw >= start + kRegionSize/2)
    assert(*reinterpret_cast<const unsigned char *>(address) == 0);
}
int main() {
  __flexfat_tbi_abi_v7();
  const uptr shadow_offset = 0x200000000000ULL;
  assert(*reinterpret_cast<const unsigned char *>(shadow_offset) == 0);
  const uptr sample_sizes[] = {32, 256, 8192};
  for (uptr size : sample_sizes) {
    void *p = __flexfat_malloc(size);
    assert(p);
    uptr tagged = reinterpret_cast<uptr>(p);
    uptr raw = Untag(tagged);
    const auto *generation = reinterpret_cast<const unsigned char *>(
        shadow_offset + (raw >> kMinSizeLog));
    assert(*generation == PointerTag(tagged));
    assert(generation[1] == 0);
    __flexfat_free(p);
    assert(*generation == static_cast<unsigned char>(PointerTag(tagged) + 1));
    assert(generation[1] == 0);
  }
  for (uptr r = 0; r < kNumSizeClasses; ++r) {
    uptr start = GetRegionStart(r), end = start + kRegionSize;
    uptr size = SizeClassToSize(r), first = start / size, last = (end-1)/size;
    assert(shadow_offset + (start >> kMinSizeLog) <
           shadow_offset + (end >> kMinSizeLog));
    for (uptr delta = 0; delta < 4096; ++delta) {
      check(r, start + delta); check(r, end - delta - 1);
    }
    uint64_t random = r + 4106;
    for (unsigned i = 0; i < 1024; ++i) {
      random = random * UINT64_C(6364136223846793005) + 1;
      uptr slot = first + random % (last-first+1);
      for (int delta = -1; delta <= 1; ++delta) {
        uptr address = slot * size + delta;
        if (address >= start && address < end) check(r, address);
      }
    }
  }
}
