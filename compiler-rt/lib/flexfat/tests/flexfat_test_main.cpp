//===-- flexfat_test_main.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  // Unit 12a: the stack regions are now MAP_SHARED, so a bare fork() shares
  // physical stack bytes between parent and child (the child segfaults the
  // moment either side touches its stack). The reference fixes this with a
  // fork interposer (lowfat_fork.c, Part III scope). Until then, run death
  // tests under "threadsafe" mode — fork+exec — which gives the child a fresh
  // process so the shared-stack alias doesn't bite.
  testing::FLAGS_gtest_death_test_style = "threadsafe";
  return RUN_ALL_TESTS();
}
