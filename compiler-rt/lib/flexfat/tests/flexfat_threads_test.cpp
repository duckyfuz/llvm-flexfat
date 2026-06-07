//===-- flexfat_threads_test.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit 14a (runtime, threads): the thread-stack pool's Fisher-Yates shuffle
// is a real permutation; lowfat_stack_alloc returns stacks inside the stack
// sub-range at class-aligned addresses; slot reclamation via the TID-final
// state (lowfat_force_stack_free → lowfat_stack_alloc round-trip) returns
// the same slot.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include <set>

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#include "lowfat.h"

extern "C" {
// Runtime internals exposed for testing (declared non-static in lowfat.c).
void *lowfat_stack_alloc(void);
void lowfat_force_stack_free(void *stack);
extern uint16_t lowfat_stack_perm[];
}

namespace {

constexpr size_t kNumThreadStacks = 128;        // LOWFAT_NUM_THREAD_STACKS
constexpr size_t kStackSize = 67108864;         // 64 MiB

TEST(FlexFatThreads, FisherYatesIsPermutation) {
  // The Fisher-Yates shuffle runs in lowfat_threads_init at startup. We test
  // the permutation property (every value in [0, 128) appears exactly once),
  // NOT a specific order — the shuffle is seeded by lowfat_rand and is
  // supposed to vary across runs.
  std::set<int> seen;
  for (size_t i = 0; i < kNumThreadStacks; i++) {
    int v = lowfat_stack_perm[i];
    ASSERT_GE(v, 0);
    ASSERT_LT(v, (int)kNumThreadStacks);
    EXPECT_TRUE(seen.insert(v).second)
        << "duplicate slot " << v << " at perm[" << i << "]";
  }
  EXPECT_EQ(seen.size(), kNumThreadStacks);
}

TEST(FlexFatThreads, AllocReturnsInStackSubrange) {
  void *stack = lowfat_stack_alloc();
  ASSERT_NE(stack, nullptr);
  // Every slot is at LOWFAT_STACKS_START + slot_idx * LOWFAT_STACK_SIZE,
  // so the address is exactly LOWFAT_STACK_SIZE-aligned.
  EXPECT_EQ((uintptr_t)stack % kStackSize, (uintptr_t)0);
  // The pointer must classify as a lowfat stack pointer (inside the
  // stack sub-range of LOWFAT_STACK_REGION).
  EXPECT_TRUE(lowfat_is_ptr(stack));
  EXPECT_TRUE(lowfat_is_stack_ptr(stack));
}

TEST(FlexFatThreads, SlotReclamationAfterForceFree) {
  // lowfat_force_stack_free constructs a fake "already-dead" pthread_t at
  // the top of the given slot (TID = -1, the final state for joined
  // threads) and pushes it onto the freelist. The next lowfat_stack_alloc
  // walks the freelist first, finds the fake thread is dead, and reclaims
  // the slot. This is the TID-path; without the build gate from this same
  // unit, a wrong TID_OFFSET would silently corrupt this reclamation.
  void *stack_a = lowfat_stack_alloc();
  ASSERT_NE(stack_a, nullptr);
  lowfat_force_stack_free(stack_a);
  void *stack_b = lowfat_stack_alloc();
  EXPECT_EQ(stack_a, stack_b);
}

// Tiny mutex-protected counter so the multi-thread stress is observable
// from the test side without relying on the allocator-internal mutexes.
struct CounterCtx {
  pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
  int n_completed = 0;
};

static void *stress_worker(void *arg) {
  CounterCtx *ctx = static_cast<CounterCtx *>(arg);
  // Hit the size-class allocator across multiple classes. lowfat_malloc is
  // exported by the runtime (Unit 4); the per-region mutex inside lowfat
  // is the synchronization under test here.
  for (int i = 0; i < 200; i++) {
    void *p1 = lowfat_malloc(16);
    void *p2 = lowfat_malloc(96);
    void *p3 = lowfat_malloc(1024);
    void *p4 = lowfat_malloc(4096);
    lowfat_free(p1);
    lowfat_free(p3);
    lowfat_free(p2);
    lowfat_free(p4);
  }
  pthread_mutex_lock(&ctx->mu);
  ctx->n_completed++;
  pthread_mutex_unlock(&ctx->mu);
  return nullptr;
}

TEST(FlexFatThreads, ConcurrentAllocStress) {
  // Real concurrency for the Unit-4 per-region mutexes. If any per-region
  // mutex is missing or broken, this either deadlocks (test framework
  // times out) or corrupts the freelist (later allocations crash).
  constexpr int kThreads = 4;
  pthread_t ts[kThreads];
  CounterCtx ctx;
  for (int i = 0; i < kThreads; i++)
    ASSERT_EQ(pthread_create(&ts[i], nullptr, stress_worker, &ctx), 0);
  for (int i = 0; i < kThreads; i++)
    ASSERT_EQ(pthread_join(ts[i], nullptr), 0);
  EXPECT_EQ(ctx.n_completed, kThreads);
}

} // namespace
