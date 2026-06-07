//===-- flexfat_classify_test.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit 5: pointer classification (SPEC §5.4). lowfat_kind() itself is internal;
// its output is checked end-to-end in test/flexfat/TestCases/oob_report.c, so
// here we reproduce its precedence from the exported classifiers.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include <stddef.h>
#include <stdint.h>

#include "lowfat.h"

namespace {

// 32 GiB-region sub-range layout (ABI constants from lowfat_config.c; the region
// layout is identical for both variants).
constexpr uintptr_t kRegionSize = (uintptr_t)1 << 35;
constexpr uintptr_t kHeapOffset = 0;
constexpr uintptr_t kGlobalOffset = 17179832320ull;
constexpr uintptr_t kStackOffset = 25769803776ull;

const char *kind(const void *p) {
  if (!lowfat_is_ptr(p))
    return "nonfat";
  if (lowfat_is_heap_ptr(p))
    return "heap";
  if (lowfat_is_stack_ptr(p))
    return "stack";
  if (lowfat_is_global_ptr(p))
    return "global";
  return "unused";
}

TEST(FlexFatClassify, HeapStackGlobalNonfat) {
  void *heap = (void *)(kRegionSize + kHeapOffset + 100);
  void *global = (void *)(kRegionSize + kGlobalOffset + 100);
  void *stack = (void *)(kRegionSize + kStackOffset + 100);
  void *nonfat = (void *)0x4000; // below region 1
  void *idx0 = (void *)0x1000;   // index 0

  EXPECT_TRUE(lowfat_is_ptr(heap));
  EXPECT_TRUE(lowfat_is_heap_ptr(heap));
  EXPECT_FALSE(lowfat_is_stack_ptr(heap));
  EXPECT_FALSE(lowfat_is_global_ptr(heap));
  EXPECT_STREQ(kind(heap), "heap");

  EXPECT_TRUE(lowfat_is_ptr(global));
  EXPECT_FALSE(lowfat_is_heap_ptr(global));
  EXPECT_FALSE(lowfat_is_stack_ptr(global));
  EXPECT_TRUE(lowfat_is_global_ptr(global));
  EXPECT_STREQ(kind(global), "global");

  EXPECT_TRUE(lowfat_is_ptr(stack));
  EXPECT_FALSE(lowfat_is_heap_ptr(stack));
  EXPECT_TRUE(lowfat_is_stack_ptr(stack));
  EXPECT_FALSE(lowfat_is_global_ptr(stack));
  EXPECT_STREQ(kind(stack), "stack");

  EXPECT_FALSE(lowfat_is_ptr(nonfat));
  EXPECT_FALSE(lowfat_is_heap_ptr(nonfat));
  EXPECT_STREQ(kind(nonfat), "nonfat");

  EXPECT_EQ(lowfat_index(idx0), (size_t)0);
  EXPECT_FALSE(lowfat_is_ptr(idx0)); // index 0 is the non-fat region
  EXPECT_STREQ(kind(idx0), "nonfat");
}

TEST(FlexFatClassify, RealHeapAllocation) {
  void *p = lowfat_malloc(64);
  ASSERT_TRUE(lowfat_is_ptr(p));
  EXPECT_TRUE(lowfat_is_heap_ptr(p));
  EXPECT_FALSE(lowfat_is_stack_ptr(p));
  EXPECT_FALSE(lowfat_is_global_ptr(p));
  EXPECT_STREQ(kind(p), "heap");
  lowfat_free(p);
}

} // namespace
