//===-- flexfat_malloc_test.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unit 4: the per-size-class bump+freelist heap allocator. The runtime is linked
// WITHOUT the libc interposition aliases (-DLOWFAT_NO_REPLACE_STD_MALLOC/FREE),
// so the test's own allocations stay on libc and these cases call lowfat_malloc/
// lowfat_free directly.
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lowfat.h"

// Index entry point (ABI; the pass's optimizeMalloc calls this in Unit 9).
extern "C" void *lowfat_malloc_index(size_t idx, size_t size);
extern "C" size_t malloc_usable_size(void *ptr);

namespace {

constexpr size_t kPage = 4096;
constexpr size_t kBigObject = 3 * kPage; // LOWFAT_BIG_OBJECT

size_t MaxHeapAlloc() {
  size_t last = 0;
  for (size_t r = 1; _LOWFAT_SIZES[r] != SIZE_MAX; ++r)
    last = _LOWFAT_SIZES[r];
  return last;
}

// Every size class: a request landing in each class round-trips through
// lowfat_base/size and is a heap pointer.
TEST(FlexFatMalloc, EverySizeClassRoundTrips) {
  int lowfat_count = 0, fallback_count = 0;
  for (size_t r = 1; _LOWFAT_SIZES[r] != SIZE_MAX; ++r) {
    size_t classsz = _LOWFAT_SIZES[r];
    size_t req = classsz - 1; // a request landing in this class (or the next)
    void *p = lowfat_malloc(req);
    if (lowfat_is_heap_ptr(p)) {
      ++lowfat_count;
      EXPECT_EQ((uintptr_t)lowfat_base(p), (uintptr_t)p) << "fresh base==self";
      EXPECT_GE(lowfat_size(p), req);
      EXPECT_GE(lowfat_buffer_size(p), req); // object fully contains the request
    } else {
      // The largest class(es) cannot fit their region after the ASLR start, so
      // they fall back to libc. That is expected allocator behavior.
      ++fallback_count;
      EXPECT_FALSE(lowfat_is_ptr(p));
    }
    lowfat_free(p);
  }
  EXPECT_GE(lowfat_count, 50) << "the vast majority of classes allocate in-region";
  EXPECT_LE(fallback_count, 4);
}

// The index ABI form lands in the requested region.
TEST(FlexFatMalloc, MallocIndex) {
  void *p = lowfat_malloc_index(1, 16); // region 1, class 16
  ASSERT_TRUE(lowfat_is_heap_ptr(p));
  EXPECT_EQ(lowfat_index(p), (size_t)1);
  EXPECT_EQ(lowfat_size(p), (size_t)16);
  EXPECT_EQ((uintptr_t)lowfat_base(p), (uintptr_t)p);
  lowfat_free(p);
}

// Freelist LIFO reuse: free A then B, malloc returns B then A.
TEST(FlexFatMalloc, FreelistLifoReuse) {
  void *a = lowfat_malloc(64);
  void *b = lowfat_malloc(64);
  ASSERT_NE(a, b);
  ASSERT_EQ(lowfat_index(a), lowfat_index(b)); // same class/region
  lowfat_free(a);
  lowfat_free(b);
  void *c = lowfat_malloc(64);
  void *d = lowfat_malloc(64);
  EXPECT_EQ(c, b) << "LIFO: last freed returned first";
  EXPECT_EQ(d, a);
  lowfat_free(c);
  lowfat_free(d);
}

// Big object (>= 3*PAGE): pages beyond the first are de-paged on free
// (madvise(DONTNEED) + mprotect(PROT_NONE)) and repaged on reuse.
TEST(FlexFatMallocDeathTest, BigObjectDePageAndRepage) {
  // Only the REQUESTED size is committed (lazy-commit); the class may be larger
  // with the surplus pages left as PROT_NONE guard pages.
  size_t req = kBigObject + kPage; // 4 requested pages, big-object class
  void *p = lowfat_malloc(req);
  ASSERT_TRUE(lowfat_is_heap_ptr(p));
  ASSERT_GE(lowfat_size(p), kBigObject); // big-object class -> de-paged on free
  memset(p, 0x11, req);                  // touch the committed pages
  lowfat_free(p);
  // Pages 2..N are now PROT_NONE; writing there faults (page 1 is the list node).
  EXPECT_DEATH({ *((volatile char *)p + kPage) = 0x22; }, "");
  // Reuse the same class -> LIFO pop -> committed pages repaged R/W.
  void *q = lowfat_malloc(req);
  ASSERT_EQ(q, p);
  memset(q, 0x33, req); // repaged -> no fault
  lowfat_free(q);
}

// realloc within the same size class returns the same pointer (no copy).
TEST(FlexFatMalloc, ReallocSameClassNoCopy) {
  void *p = lowfat_malloc(40); // class 48 range
  ASSERT_TRUE(lowfat_is_heap_ptr(p));
  memset(p, 0xAB, 40);
  void *q = lowfat_realloc(p, 45); // same class -> same ptr
  EXPECT_EQ(q, p);
  EXPECT_EQ(((unsigned char *)q)[0], 0xAB); // memory untouched (no copy)
  lowfat_free(q);
}

// The alignment family round-trips.
TEST(FlexFatMalloc, AlignmentFamily) {
  void *p = nullptr;
  ASSERT_EQ(lowfat_posix_memalign(&p, 64, 100), 0);
  EXPECT_EQ((uintptr_t)p % 64, (uintptr_t)0);
  EXPECT_TRUE(lowfat_is_heap_ptr(p));
  lowfat_free(p);

  void *m = lowfat_memalign(128, 200);
  EXPECT_EQ((uintptr_t)m % 128, (uintptr_t)0);
  EXPECT_TRUE(lowfat_is_heap_ptr(m));
  lowfat_free(m);

  void *a = lowfat_aligned_alloc(256, 300);
  EXPECT_EQ((uintptr_t)a % 256, (uintptr_t)0);
  EXPECT_TRUE(lowfat_is_heap_ptr(a));
  lowfat_free(a);

  void *v = lowfat_valloc(100);
  EXPECT_EQ((uintptr_t)v % kPage, (uintptr_t)0);
  EXPECT_TRUE(lowfat_is_heap_ptr(v));
  lowfat_free(v);

  void *pv = lowfat_pvalloc(100);
  EXPECT_EQ((uintptr_t)pv % kPage, (uintptr_t)0);
  EXPECT_TRUE(lowfat_is_heap_ptr(pv));
  lowfat_free(pv);
}

// calloc zeroes; strdup/strndup copy; malloc_usable_size reports the class size.
TEST(FlexFatMalloc, CallocStrdupUsableSize) {
  char *z = (char *)lowfat_calloc(10, 8);
  ASSERT_TRUE(lowfat_is_heap_ptr(z));
  for (int i = 0; i < 80; ++i)
    EXPECT_EQ(z[i], 0);
  EXPECT_EQ(malloc_usable_size(z), lowfat_size(z));
  lowfat_free(z);

  const char *s = "hello flexfat";
  char *d = lowfat_strdup(s);
  ASSERT_TRUE(lowfat_is_heap_ptr(d));
  EXPECT_STREQ(d, s);
  lowfat_free(d);

  char *d2 = lowfat_strndup(s, 5);
  ASSERT_TRUE(lowfat_is_heap_ptr(d2));
  EXPECT_STREQ(d2, "hello");
  lowfat_free(d2);
}

// libc fallback: too-large request (heap_select -> 0) returns a non-lowfat ptr.
TEST(FlexFatMalloc, FallbackTooLarge) {
  size_t big = MaxHeapAlloc() * 2; // bigger than any class
  void *p = lowfat_malloc(big);
  ASSERT_NE(p, nullptr);
  EXPECT_FALSE(lowfat_is_ptr(p)) << "too-large -> libc fallback (non-lowfat)";
  lowfat_free(p); // routes back to libc free
}

// libc fallback: exhausting a region falls back to libc.
TEST(FlexFatMalloc, FallbackRegionOverflow) {
  // ~3 GiB requests land in a ~4 GiB class; a 16 GiB region holds a few, then
  // overflows to libc (objects are never touched, so this costs no RAM).
  size_t req = (size_t)3 << 30;
  bool saw_lowfat = false, saw_fallback = false;
  void *kept[16] = {nullptr};
  int n = 0;
  for (int i = 0; i < 16; ++i) {
    void *p = lowfat_malloc(req);
    if (lowfat_is_ptr(p))
      saw_lowfat = true;
    else
      saw_fallback = true;
    kept[n++] = p;
    if (saw_fallback)
      break;
  }
  EXPECT_TRUE(saw_lowfat) << "at least one allocation came from the region";
  EXPECT_TRUE(saw_fallback) << "the region eventually overflows to libc";
  for (int i = 0; i < n; ++i)
    lowfat_free(kept[i]);
}

// lowfat_free on a non-heap lowfat pointer (stack/global range) raises an error.
TEST(FlexFatMallocDeathTest, FreeNonHeapPointerErrors) {
  // A lowfat pointer in region 1 but past the heap sub-range (heap is ~16 GiB).
  void *bad = (void *)((uintptr_t)1 * ((uintptr_t)1 << 35) + ((uintptr_t)20 << 30));
  ASSERT_TRUE(lowfat_is_ptr(bad));
  ASSERT_FALSE(lowfat_is_heap_ptr(bad));
  EXPECT_DEATH({ lowfat_free(bad); }, "attempt to free a");
}

} // namespace
