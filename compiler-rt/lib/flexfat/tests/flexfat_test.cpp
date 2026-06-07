//===-- flexfat_test.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit 1 sentinel for the FlexFat runtime gtest harness. Real runtime-internals
// tests (pointer encoding, lowfat_base, the heap allocator) arrive with the
// ported runtime.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

namespace {

TEST(FlexFat, Sentinel) { EXPECT_TRUE(true); }

} // namespace
