//===-- lowfat_malloc_internal.h - FlexFat allocator bookkeeping ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The allocator's pure bookkeeping: the freelist node layout and the page
// arithmetic macros (no fixed-region addresses). Shared by lowfat_malloc.c and
// the ASan/UBSan logic-only unit test (tests/logic/), which exercises this
// arithmetic on ordinary memory — the full fixed-address runtime cannot boot
// under ASan (see docs/STATUS.md), so this is where the bookkeeping gets
// sanitizer coverage.
//
//===----------------------------------------------------------------------===//
#ifndef FLEXFAT_LOWFAT_MALLOC_INTERNAL_H
#define FLEXFAT_LOWFAT_MALLOC_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#ifndef LOWFAT_PAGE_SIZE
#define LOWFAT_PAGE_SIZE 4096
#endif

#define LOWFAT_BIG_OBJECT (3 * LOWFAT_PAGE_SIZE)
#define LOWFAT_NUM_PAGES(size) ((((size) - 1) / LOWFAT_PAGE_SIZE) + 1)
#define LOWFAT_PAGES_BASE(ptr)                                                  \
  ((void *)((uint8_t *)(ptr) - ((uintptr_t)(ptr) % LOWFAT_PAGE_SIZE)))
#define LOWFAT_PAGES_SIZE(ptr, size)                                            \
  (LOWFAT_NUM_PAGES(((uint8_t *)(ptr) - (uint8_t *)LOWFAT_PAGES_BASE(ptr)) +    \
                    (size)) *                                                   \
   LOWFAT_PAGE_SIZE)

// A freed object's first word is reserved; the second links the LIFO freelist.
struct lowfat_freelist_s {
  uintptr_t _reserved; // Reserved for meta-data.
  struct lowfat_freelist_s *next;
};
typedef struct lowfat_freelist_s *lowfat_freelist_t;

#endif // FLEXFAT_LOWFAT_MALLOC_INTERNAL_H
