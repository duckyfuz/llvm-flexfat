//===-- flexfat_encoding_test.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit 3: pointer-encoding core. The flexfat runtime is compiled with the
// non-POW2 (default) config, so the table-driven cases exercise the non-POW2
// reciprocal path and reproduce SPEC §1.4. The POW2 base/magic formulas are
// validated independently (pure math), since a single runtime build fixes one
// variant.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "lowfat.h"

namespace {

constexpr uintptr_t kRegionSize = (uintptr_t)1 << 35; // 32 GiB

uintptr_t TruthBase(uintptr_t p, size_t size) { return p - (p % size); }

uint64_t Pow2Magic(size_t size) { return ~(uint64_t)(size - 1); }
uint64_t NonPow2Magic(size_t size) {
  unsigned __int128 r = (unsigned __int128)UINT64_MAX + 1; // 2^64
  return (uint64_t)(r / (unsigned __int128)size) + 1;      // floor(2^64/size)+1
}
uintptr_t Pow2Base(uintptr_t p, uint64_t magic) { return p & magic; }
uintptr_t NonPow2Base(uintptr_t p, size_t size, uint64_t magic) {
  unsigned __int128 t = (unsigned __int128)magic * (unsigned __int128)p;
  return (uintptr_t)(t >> 64) * size; // reciprocal multiply, no div
}

// ---- POW2 variant: magic = ~(size-1), base = p & magic ----
TEST(FlexFatEncoding, Pow2BaseFormula) {
  for (int k = 4; k <= 33; ++k) { // sizes 16 .. 2^33
    size_t size = (size_t)1 << k;
    uint64_t magic = Pow2Magic(size);
    EXPECT_EQ(magic, ~(uint64_t)(size - 1));
    for (uintptr_t r = 1; r <= 5; ++r) {
      uintptr_t rbase = r * kRegionSize;
      uintptr_t offs[] = {0, 1, size / 2, size - 1, size, size + 3, 7 * size};
      for (uintptr_t off : offs) {
        uintptr_t p = rbase + off;
        EXPECT_EQ(Pow2Base(p, magic), TruthBase(p, size))
            << "size=" << size << " p=" << p;
      }
    }
  }
}

// ---- non-POW2 variant: reciprocal multiply == floor(p/size)*size ----
TEST(FlexFatEncoding, NonPow2ReciprocalNoDiv) {
  size_t sizes[] = {16,  48,  80,  96,   112,  144,  160,  192, 224,
                    272, 320, 384, 448,  528,  640,  768,  896, 1040,
                    1280,1536,1792,2064, 2560, 3072, 3584, 4112};
  for (size_t size : sizes) {
    uint64_t magic = NonPow2Magic(size);
    uintptr_t rbase = kRegionSize; // region 1; stay near the bottom (exact)
    for (uintptr_t i = 0; i < 4096; ++i) {
      uintptr_t obj = rbase + i * size;
      uintptr_t ds[] = {0, 1, size / 2, size - 1};
      for (uintptr_t d : ds) {
        uintptr_t p = obj + d;
        EXPECT_EQ(NonPow2Base(p, size, magic), TruthBase(p, size))
            << "size=" << size << " p=" << p;
      }
    }
  }
}

// ---- the real runtime tables (non-POW2), incl. index-0 non-fat semantics ----
// Unit 17: the concrete table values are non-POW2-specific (61 regions with
// the reciprocal-magic schedule from sizes.cfg). POW2 has 30 regions with a
// different magic schedule; the equivalent POW2 sweep is the
// Pow2BaseFormula case above, which covers the encoding analytically.
#if !FLEXFAT_IS_POW2
TEST(FlexFatEncoding, RuntimeTablesAndIndexZero) {
  // Index 0 is the non-fat region.
  EXPECT_EQ(_LOWFAT_SIZES[0], SIZE_MAX);
  EXPECT_EQ(_LOWFAT_MAGICS[0], (uint64_t)0);
  void *low = (void *)0x4000; // below region 1 -> non-fat
  EXPECT_FALSE(lowfat_is_ptr(low));
  EXPECT_EQ(lowfat_index(low), (size_t)0);
  EXPECT_EQ(lowfat_size(low), SIZE_MAX);
  EXPECT_EQ(lowfat_magic(low), (size_t)0);
  EXPECT_EQ((uintptr_t)lowfat_base(low), (uintptr_t)0); // magic0=0 -> NULL base

  // Sweep every size-class region via the initialised tables.
  for (size_t r = 1; _LOWFAT_SIZES[r] != SIZE_MAX; ++r) {
    size_t size = _LOWFAT_SIZES[r];
    uint64_t mag = _LOWFAT_MAGICS[r];
    uintptr_t q = (uintptr_t)r * kRegionSize + 3 * size + 7; // interior ptr
    EXPECT_EQ(lowfat_index((void *)q), r) << "r=" << r;
    EXPECT_EQ(lowfat_size((void *)q), size) << "r=" << r;
    EXPECT_EQ(lowfat_magic((void *)q), (size_t)mag) << "r=" << r;
    EXPECT_EQ((uintptr_t)lowfat_base((void *)q), TruthBase(q, size)) << "r=" << r;
    EXPECT_EQ(lowfat_buffer_size((void *)q), size - (q - TruthBase(q, size)))
        << "r=" << r;
  }
}

#endif  // !FLEXFAT_IS_POW2

// ---- SPEC §1.4 worked example, reproduced via the ported lowfat-ptr-info ----
// Unit 17: SPEC §1.4's worked example uses non-POW2 encoding (size=16,
// reciprocal magic = 0x1000000000000001). POW2 has a different magic schedule;
// this case is non-POW2-only.
#if !FLEXFAT_IS_POW2
TEST(FlexFatEncoding, PtrInfoWorkedExample) {
  void *q = (void *)0x8997f2825ull;
  EXPECT_TRUE(lowfat_is_heap_ptr(q));
  EXPECT_EQ(lowfat_index(q), (size_t)1);
  EXPECT_EQ((uintptr_t)lowfat_base(q), (uintptr_t)0x8997f2820ull);
  EXPECT_EQ(lowfat_size(q), (size_t)16);
  EXPECT_EQ(lowfat_magic(q), (size_t)0x1000000000000001ull);
  uintptr_t offset = (uintptr_t)q - (uintptr_t)lowfat_base(q);
  EXPECT_EQ(offset, (uintptr_t)5);

  // get(q, 20) reads q+20; diff = (q+20) - base = 0x19 = 25 >= 16 -> OOB.
  uintptr_t base = (uintptr_t)lowfat_base(q);
  uintptr_t diff = ((uintptr_t)q + 20) - base;
  EXPECT_EQ(diff, (uintptr_t)0x19);
  EXPECT_GE(diff, lowfat_size(q));

  // Reproduce the exact lowfat-ptr-info output (golden).
  char buf[256];
  snprintf(buf, sizeof(buf),
           "ptr    = %p\ntype   = %s\nregion = #%zu (%p)\nbase   = %p\n"
           "size   = %zu (0x%zx)\nmagic  = %zu (0x%zx)\noffset = %zu\n",
           q, "heap", lowfat_index(q),
           (void *)(lowfat_index(q) * (uintptr_t)kRegionSize), lowfat_base(q),
           lowfat_size(q), lowfat_size(q), lowfat_magic(q), lowfat_magic(q),
           (size_t)offset);
  EXPECT_STREQ(buf,
               "ptr    = 0x8997f2825\n"
               "type   = heap\n"
               "region = #1 (0x800000000)\n"
               "base   = 0x8997f2820\n"
               "size   = 16 (0x10)\n"
               "magic  = 1152921504606846977 (0x1000000000000001)\n"
               "offset = 5\n");
}
#endif  // !FLEXFAT_IS_POW2

// ---- the SIZES/MAGICS tables are read-only after init ----
TEST(FlexFatEncodingDeathTest, TablesReadOnly) {
  EXPECT_DEATH({ *(volatile size_t *)(_LOWFAT_SIZES + 5) = 0; }, "");
  EXPECT_DEATH({ *(volatile uint64_t *)(_LOWFAT_MAGICS + 5) = 0; }, "");
}

} // namespace
