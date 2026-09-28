// Compiled without instrumentation by run-temporal.py, with matching config.
#include "flexfat/flexfat_config.h"
#include "flexfat/flexfat_interface.h"
#include <assert.h>
#include <stdint.h>
using namespace __flexfat;

static void check(uptr region, uptr raw) {
  uptr size = SizeClassToSize(region);
  auto &d = __flexfat_temporal_regions_v2[region];
#ifdef FLEXFAT_CUSTOM_CONFIG
  uptr table = raw >> kRegionSizeLog;
  auto *sizes = reinterpret_cast<const uptr *>(kTablesBase);
  auto *magics = reinterpret_cast<const uptr *>(kTablesBase + kTablesOffset);
  assert(sizes[table] == size);
  uptr q = (static_cast<unsigned __int128>(raw) * magics[table]) >> 64;
  unsigned __int128 candidate = static_cast<unsigned __int128>(q) * size;
  assert(candidate <= UINT64_MAX);
  q -= q * size > raw;
#else
  uptr q = raw >> (region + 4);
#endif
  assert(q == raw / size);
  uptr index = q - d.first_slot_number;
  uptr base = raw - raw % size;
  uptr first = ((GetRegionStart(region) + size - 1) / size) * size;
  uptr end = GetRegionStart(region) + kRegionSize;
  bool reference_valid = base >= first && base + size <= end;
  assert((index < d.slot_count) == reference_valid);
  if (reference_valid)
    assert(index == (base - first) / size);
}

int main() {
  __flexfat_tbi_abi_v2();
  for (uptr r = 0; r < kNumSizeClasses; ++r) {
    uptr start = GetRegionStart(r), end = start + kRegionSize;
    uptr size = SizeClassToSize(r);
    uptr first = (start + size - 1) / size;
    uptr count = (end - first * size) / size;
    auto &d = __flexfat_temporal_regions_v2[r];
    assert(d.first_slot_number == first && d.slot_count == count);
    assert(d.metadata_base);
    if (r)
      assert(d.metadata_base == __flexfat_temporal_regions_v2[r - 1].metadata_base +
                                    __flexfat_temporal_regions_v2[r - 1].slot_count);
    check(r, start); check(r, end - 1);
    uptr slots[] = {first, first + 1, first + count / 2,
                   first + count - 1, first + count};
    for (uptr slot : slots) {
      uptr boundary = slot * size;
      for (int delta = -1; delta <= 1; ++delta) {
        uptr address = boundary + delta;
        if (address >= start && address < end) check(r, address);
      }
    }
    for (uptr part = 1; part < 64; ++part)
      check(r, start + (kRegionSize / 64) * part);
  }
}
