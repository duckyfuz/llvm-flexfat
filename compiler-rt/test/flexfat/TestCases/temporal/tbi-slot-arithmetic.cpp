// REQUIRES: flexfat-tbi
// RUN: %clang -O2 %flexfat_config_flags -c %s -o %t.o
// RUN: %clangxx_flexfat_tbi %t.o -o %t && %run %t

// Compiled without instrumentation, with the matching generated configuration.
#include "flexfat/flexfat_config.h"
#include "flexfat/flexfat_interface.h"
#include <assert.h>
#include <stdint.h>
using namespace __flexfat;

static uptr corrected(uptr raw, uptr size, uptr magic) {
  uptr q = (static_cast<unsigned __int128>(raw) * magic) >> 64;
  assert(static_cast<unsigned __int128>(q) * size <= UINT64_MAX);
  return q - (q * size > raw);
}
static void check(uptr r, uptr raw) {
  const uptr size = SizeClassToSize(r);
  const uptr index = raw >> kRegionSizeLog;
  const auto *sizes = reinterpret_cast<const uptr *>(kTablesBase);
  const auto *biases = reinterpret_cast<const uptr *>(kTablesBase + 2*kTablesOffset);
  assert(sizes[index] == size);
#ifdef FLEXFAT_CUSTOM_CONFIG
  const auto *magics = reinterpret_cast<const uptr *>(kTablesBase + kTablesOffset);
  uptr q = corrected(raw, size, magics[index]);
#else
  uptr q = raw >> (r + 4);
#endif
  assert(q == raw / size);
  uptr start = GetRegionStart(r), end = start + kRegionSize;
  uptr first = start / size, last = (end - 1) / size;
  uptr begin = biases[index] + first, limit = biases[index] + last + 1;
  uptr address = biases[index] + q;
  assert(address >= begin && address < limit);
#ifndef FLEXFAT_CUSTOM_CONFIG
  assert(address == (index << (kRegionSizeLog - kMinSizeLog)) +
                        ((raw & (kRegionSize - 1)) >> (r + kMinSizeLog)));
#endif
  // Runtime/CRT startup may allocate low slots before main. Partial slots
  // and the distant upper half have never been allocated by this test.
  if (q * size < start || q * size > end - size || raw >= start + kRegionSize/2)
    assert(*reinterpret_cast<const unsigned char *>(address) == 0);
}
int main() {
#ifdef FLEXFAT_CUSTOM_CONFIG
  __flexfat_tbi_abi_v3();
#else
  __flexfat_tbi_abi_v4();
#endif
  const auto *biases = reinterpret_cast<const uptr *>(kTablesBase + 2*kTablesOffset);
  const uptr managed = kRegionBase >> kRegionSizeLog;
  assert(biases[0] && *reinterpret_cast<const unsigned char *>(biases[0]) == 0);
  for (uptr index = 0; index < (kUserAddressLimit >> kRegionSizeLog); ++index)
    if (index < managed || index >= managed + kNumSizeClasses)
      assert(biases[index] == biases[0]);
#ifdef FLEXFAT_CUSTOM_CONFIG
  uptr previous_end = 0;
#endif
  for (uptr r = 0; r < kNumSizeClasses; ++r) {
    uptr start = GetRegionStart(r), end = start + kRegionSize;
    uptr size = SizeClassToSize(r), first = start / size, last = (end-1)/size;
    uptr begin = biases[start >> kRegionSizeLog] + first;
#ifdef FLEXFAT_CUSTOM_CONFIG
    if (r) assert(begin == previous_end);
    previous_end = biases[start >> kRegionSizeLog] + last + 1;
#else
    assert(begin ==
           ((start >> kRegionSizeLog) << (kRegionSizeLog - kMinSizeLog)));
#endif
    // Exhaust every byte near each region edge, including both paddings.
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
  // Exhaust small synthetic geometries, not just sampled production classes.
  for (uptr size = 2; size <= 256; ++size) {
    uptr magic = UINT64_MAX / size + 1;
    for (uptr raw = 0; raw < 65536; ++raw)
      assert(corrected(raw, size, magic) == raw / size);
    for (uptr start = 0; start < 256; ++start) {
      uptr first = start / size, last = (start + 256 - 1) / size;
      uptr bias = uptr(17) - first;
      for (uptr raw = start; raw < start+256; ++raw)
        assert(bias+raw/size >= 17 && bias+raw/size <= 17+last-first);
    }
  }
}
