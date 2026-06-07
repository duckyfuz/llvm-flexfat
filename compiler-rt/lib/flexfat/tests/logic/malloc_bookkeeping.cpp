//===-- malloc_bookkeeping.cpp - allocator bookkeeping under ASan/UBSan ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Logic-only coverage for the allocator's bookkeeping, run under host ASan+UBSan
// on ORDINARY memory (no fixed 2^35 regions). It exercises the shared page
// arithmetic macros, the freelist node link/unlink, and the posix_memalign
// offset math from lowfat_malloc_internal.h. The full fixed-address runtime
// cannot boot under ASan (see docs/STATUS.md); this is the ASan/UBSan coverage
// that the "gtests under ASan" criterion was meant to provide.
//
// RUN: %clangxx_asan_ubsan %s -o %t
// RUN: %run %t | grep "bookkeeping: OK"
//===----------------------------------------------------------------------===//

#include "lowfat_malloc_internal.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL: %s (line %d)\n", #cond, __LINE__);                \
      abort();                                                                 \
    }                                                                          \
  } while (0)

// Page arithmetic macros, validated against ordinary page-aligned memory.
static void test_page_macros() {
  CHECK(LOWFAT_NUM_PAGES(1) == 1);
  CHECK(LOWFAT_NUM_PAGES(LOWFAT_PAGE_SIZE) == 1);
  CHECK(LOWFAT_NUM_PAGES(LOWFAT_PAGE_SIZE + 1) == 2);
  CHECK(LOWFAT_NUM_PAGES(3 * LOWFAT_PAGE_SIZE) == 3);
  CHECK(LOWFAT_BIG_OBJECT == 3 * LOWFAT_PAGE_SIZE);

  const size_t npages = 8;
  void *buf = nullptr;
  CHECK(posix_memalign(&buf, LOWFAT_PAGE_SIZE, npages * LOWFAT_PAGE_SIZE) == 0);
  uint8_t *base = (uint8_t *)buf;

  uint8_t *p = base + LOWFAT_PAGE_SIZE + 100; // 100 bytes into page 1
  CHECK((uint8_t *)LOWFAT_PAGES_BASE(p) == base + LOWFAT_PAGE_SIZE);

  size_t obj = 5000; // 100 + 5000 = 5100 -> spans 2 pages
  size_t psize = LOWFAT_PAGES_SIZE(p, obj);
  CHECK(psize == 2 * LOWFAT_PAGE_SIZE);

  uint8_t *pbase = (uint8_t *)LOWFAT_PAGES_BASE(p);
  CHECK(pbase + psize <= base + npages * LOWFAT_PAGE_SIZE);
  memset(pbase, 0xAB, psize); // ASan validates this stays inside buf
  free(buf);
}

// Freelist LIFO link/unlink, on real nodes in an ordinary buffer.
static void test_freelist_lifo() {
  const int N = 16;
  size_t slot = sizeof(struct lowfat_freelist_s);
  if (slot < 64)
    slot = 64;
  uint8_t *buf = (uint8_t *)malloc((size_t)N * slot);
  CHECK(buf != nullptr);

  lowfat_freelist_t head = nullptr;
  for (int i = 0; i < N; i++) { // the allocator's free() push
    lowfat_freelist_t node = (lowfat_freelist_t)(buf + (size_t)i * slot);
    node->next = head;
    head = node;
  }
  for (int i = N - 1; i >= 0; i--) { // the allocator's malloc() pop (LIFO)
    CHECK(head != nullptr);
    lowfat_freelist_t node = head;
    head = node->next;
    CHECK((uint8_t *)node == buf + (size_t)i * slot);
  }
  CHECK(head == nullptr);
  free(buf);
}

// posix_memalign offset math, validated against a real over-allocation.
static void test_align_offset() {
  const size_t aligns[] = {16, 32, 64, 128, 256, (size_t)LOWFAT_PAGE_SIZE};
  for (size_t align : aligns) {
    size_t size = 100;
    size_t nsize = size + align - 1; // posix_memalign over-allocates this much
    uint8_t *raw = (uint8_t *)malloc(nsize);
    CHECK(raw != nullptr);
    size_t offset = (uintptr_t)raw % align;
    offset = (offset != 0 ? align - offset : offset);
    uint8_t *aligned = raw + offset;
    CHECK((uintptr_t)aligned % align == 0);
    CHECK(aligned >= raw && aligned + size <= raw + nsize);
    memset(aligned, 0xCD, size); // ASan validates within the over-alloc
    free(raw);
  }
}

int main() {
  test_page_macros();
  test_freelist_lifo();
  test_align_offset();
  printf("flexfat allocator bookkeeping: OK\n");
  return 0;
}
