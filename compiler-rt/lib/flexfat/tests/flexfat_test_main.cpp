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
  // Unit 12a installed a "threadsafe" death-test mitigation here because
  // the MAP_SHARED stack regions caused bare fork() in gtest's fast-mode
  // death tests to segfault. Unit 14b lands the fork interposer (clone()
  // onto a temp stack, fresh SHM stacks for the child, copy parent's
  // stack, longjmp back), which closes that hazard — fork() in the
  // child now sees private stack bytes. The default fast-mode death
  // tests are safe again; the explicit threadsafe override is removed.
  return RUN_ALL_TESTS();
}
