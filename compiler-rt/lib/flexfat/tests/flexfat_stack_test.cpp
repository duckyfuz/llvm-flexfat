//===-- flexfat_stack_test.cpp --------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit 12a (runtime): SHM helper + stack-region MAP_SHARED aliasing + stack
// table indexing. The pivot itself is exercised by the e2e
// pivot_classifies_stack.c.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#include "lowfat.h"

// Runtime internals not in the public header.
extern "C" {
int lowfat_create_shm(size_t size);
extern const size_t lowfat_stack_sizes[64 + 1];
extern const size_t lowfat_stack_masks[64 + 1];
extern const ssize_t lowfat_stack_offsets[64 + 1];
}

namespace {

TEST(FlexFatStack, ShmAliasing) {
  // A single anonymous-shm fd backs two distinct virtual mappings with
  // MAP_SHARED, so a write at one VA is visible at the other VA. This is
  // exactly the mechanism the stack-region init relies on (same physical bytes
  // at every size-class region's stack sub-range).
  const size_t kPage = 4096;
  int fd = lowfat_create_shm(kPage);
  ASSERT_GE(fd, 0) << "lowfat_create_shm failed";

  void *a = mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  void *b = mmap(nullptr, kPage, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  ASSERT_NE(a, MAP_FAILED);
  ASSERT_NE(b, MAP_FAILED);
  ASSERT_NE(a, b);

  ((volatile unsigned char *)a)[0] = 0x42;
  ((volatile unsigned char *)a)[1] = 0x99;
  EXPECT_EQ(((volatile unsigned char *)b)[0], 0x42);
  EXPECT_EQ(((volatile unsigned char *)b)[1], 0x99);

  ((volatile unsigned char *)b)[2] = 0xAB;
  EXPECT_EQ(((volatile unsigned char *)a)[2], 0xAB);

  munmap(a, kPage);
  munmap(b, kPage);
  close(fd);
}

TEST(FlexFatStack, StackTableIndexing) {
  // The stack tables are indexed by __builtin_clzll(size). The reference uses
  // `clzll(size)` (not `clzll(size-1)`) deliberately — for an exact power-of-2,
  // the lookup returns the NEXT class up, guaranteeing room for the
  // one-past-end byte. So clzll(16)=59 looks up sizes[59]=32 (not 16); the
  // size-16 entry sizes[60] is reached by sizes whose ALLOC size is 16 — i.e.
  // there is no such alloc, since 16-byte allocs round up to 32 by this rule.
  size_t idx16 = (size_t)__builtin_clzll((unsigned long long)16);
  EXPECT_EQ(idx16, (size_t)59);
  EXPECT_EQ(lowfat_stack_sizes[idx16], (size_t)32);
  EXPECT_EQ(lowfat_stack_masks[idx16], (size_t)0xFFFFFFFFFFFFFFE0ull);

  size_t idx4k = (size_t)__builtin_clzll((unsigned long long)4096);
  EXPECT_EQ(idx4k, (size_t)51);
  EXPECT_EQ(lowfat_stack_sizes[idx4k], (size_t)8192);
  EXPECT_EQ(lowfat_stack_masks[idx4k], (size_t)0xFFFFFFFFFFFFE000ull);

  // Offsets are negative — they take a master-region address back into a
  // size-class region (the "mirror" delta). Non-zero for populated classes.
  EXPECT_LT(lowfat_stack_offsets[idx16], (ssize_t)0);
  EXPECT_LT(lowfat_stack_offsets[idx4k], (ssize_t)0);
}

TEST(FlexFatStack, StackRegionAliasing) {
  // After lowfat_init, the stack sub-range of every entry in lowfat_stacks[]
  // is mapped MAP_SHARED to the same fd, so a byte written through the
  // master-region (LOWFAT_STACK_REGION) stack address is visible at the
  // mirror address in each size-class region. We don't disturb the live stack
  // we are running on: read a byte from the master region (which is committed
  // for the pivoted main stack) and check we see the same value through the
  // mirror in another size-class region.
  //
  // The active stack slot is the one the pivot chose; we read a couple of
  // bytes near the top of the (large) master region range and compare to the
  // mirror.
  //
  // If MAP_SHARED is not wired correctly (e.g. if MAP_PRIVATE is used
  // instead), the value at the mirror is whatever the kernel-zero-page
  // provides, NOT what the master region holds.

  // Master region begins at LOWFAT_STACK_REGION * LOWFAT_REGION_SIZE +
  // LOWFAT_STACK_MEMORY_OFFSET. Our live stack lives somewhere in that range.
  // Read whatever value happens to be at our own stack pointer +/- a known
  // offset and compare it against the mirror.

  volatile int probe = 0xCAFEBABE;
  uintptr_t probe_addr = (uintptr_t)&probe;
  // Sanity: we must already be on a lowfat stack — the constructor's pivot
  // ran before main, and gtest's TEST body executes from main.
  ASSERT_TRUE(lowfat_is_ptr((const void *)probe_addr));
  ASSERT_TRUE(lowfat_is_stack_ptr((const void *)probe_addr));

  // Pick a size-class index that is populated; idx 60 (size=16).
  size_t idx = (size_t)__builtin_clzll((unsigned long long)16);
  ssize_t off = lowfat_stack_offsets[idx];
  ASSERT_LT(off, (ssize_t)0);

  // Mirror address: probe + offset; this should land in size-class region's
  // stack sub-range, and through MAP_SHARED hold the same value as probe.
  const volatile int *mirror = (const volatile int *)((const char *)&probe + off);
  EXPECT_EQ(*mirror, 0xCAFEBABE);

  // Mutating through the mirror is also reflected at the master.
  *((volatile int *)mirror) = 0xDEADBEEF;
  EXPECT_EQ(probe, 0xDEADBEEF);
}

} // namespace
