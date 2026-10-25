// REQUIRES: flexfat-tbi
// UNSUPPORTED: flexfat-custom-config
// RUN: %clang -O2 %flexfat_config_flags -c %s -o %t.o
// RUN: %clangxx_flexfat_tbi -fsanitize-flexfat-tbi=packed-shadow %t.o -o %t && %run %t

#include "flexfat/flexfat_config.h"
#include "flexfat/flexfat_interface.h"
#include <assert.h>
#include <initializer_list>
#include <stdint.h>
using namespace __flexfat;
static constexpr uptr Anchor = 0x200000000000ULL;
static constexpr uptr DataBytes = 1ULL << 29;
static uptr address(uptr raw, unsigned shift) {
  s64 biased = s64(raw & 0xffffffffULL) - s64(1ULL << 33);
  return Anchor + uptr(biased >> shift);
}
int main() {
  __flexfat_tbi_abi_packed_shadow_pow2_v1();
  auto *sentinel = reinterpret_cast<volatile unsigned char *>(Anchor);
  assert(*sentinel == 0);
  uptr prefix = 0;
  for (unsigned c = 0; c < kNumSizeClasses; ++c) {
    unsigned shift = c + kMinSizeLog;
    uptr size = SizeClassToSize(c), count = kRegionSize / size;
    assert(prefix == DataBytes - ((1ULL << 33) >> shift));
    uint64_t random = c + 4106;
    for (unsigned i = 0; i < 504; ++i) {
      random = random * UINT64_C(6364136223846793005) + 1;
      uptr slot = i == 0   ? 0
                  : i == 1 ? 1
                  : i == 2 ? count / 2
                  : i == 3 ? count - 1
                           : random % count;
      for (uptr interior : {uptr(0), size / 2, size - 1}) {
        uptr raw = GetRegionStart(c) + slot * size + interior;
        uptr expected = Anchor - DataBytes + prefix + slot;
        assert(address(raw, shift) == expected);
        assert(expected >= Anchor - DataBytes && expected < Anchor - 4);
      }
    }
    for (unsigned repeat = 0; repeat < 3; ++repeat) {
      void *p = __flexfat_malloc(size - 1);
      assert(p);
      uptr tagged = reinterpret_cast<uptr>(p), raw = Untag(tagged);
      assert(((raw - kRegionBase) >> kRegionSizeLog) == c);
      auto *generation = reinterpret_cast<volatile unsigned char *>(
          Anchor - DataBytes + prefix + ((raw & 0xffffffffULL) / size));
      assert(*generation == PointerTag(tagged));
      __flexfat_free(p);
      assert(*generation == static_cast<unsigned char>(PointerTag(tagged) + 1));
      assert(*sentinel == 0);
    }
    prefix += count;
  }
  assert(prefix == DataBytes - 4);
}
