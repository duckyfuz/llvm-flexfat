//===-- flexfat_layout.h - Shared FlexFat layout -------------------*- C -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FLEXFAT_LAYOUT_H
#define FLEXFAT_LAYOUT_H

#include <stdint.h>

// This header is shared by the compiler pass, runtime, and configuration
// generator. Keep it independent of LLVM and sanitizer runtime headers.
#define FLEXFAT_MIN_SIZE_LOG 4
#define FLEXFAT_POW2_MAX_SIZE_LOG 30
#define FLEXFAT_REGION_BASE UINT64_C(0x100000000000)
#define FLEXFAT_TABLES_OFFSET UINT64_C(0x1000000)
#define FLEXFAT_TABLES_SPAN (4 * FLEXFAT_TABLES_OFFSET)
#define FLEXFAT_USER_ADDRESS_LIMIT (UINT64_C(1) << 48)
#define FLEXFAT_TEMPORAL_SHADOW_OFFSET UINT64_C(0x200000000000)
#define FLEXFAT_DIRECT_SHADOW_BYTES (UINT64_C(1) << 44)

// The generator validates custom profiles against this geometry before a
// generated profile exists. Its output supplies only class sizes and reciprocals.
#define FLEXFAT_CUSTOM_REGION_SIZE_LOG 38
#define FLEXFAT_CUSTOM_REGION_SIZE                                             \
  (UINT64_C(1) << FLEXFAT_CUSTOM_REGION_SIZE_LOG)
#define FLEXFAT_CUSTOM_TABLES_BASE UINT64_C(0x300000000000)

#ifdef FLEXFAT_CUSTOM_CONFIG
#define FLEXFAT_REGION_SIZE_LOG FLEXFAT_CUSTOM_REGION_SIZE_LOG
#define FLEXFAT_TABLES_BASE FLEXFAT_CUSTOM_TABLES_BASE
#include "flexfat_config_generated.h"
#else
#define FLEXFAT_REGION_SIZE_LOG 32
#define FLEXFAT_TABLES_BASE UINT64_C(0x118000000000)
#define FLEXFAT_NUM_SIZE_CLASSES                                               \
  (FLEXFAT_POW2_MAX_SIZE_LOG - FLEXFAT_MIN_SIZE_LOG + 1)
#define FLEXFAT_MAX_SIZE (UINT64_C(1) << FLEXFAT_POW2_MAX_SIZE_LOG)
#endif

#define FLEXFAT_REGION_SIZE (UINT64_C(1) << FLEXFAT_REGION_SIZE_LOG)

#ifdef __cplusplus
#define FLEXFAT_LAYOUT_ASSERT static_assert
#else
#define FLEXFAT_LAYOUT_ASSERT _Static_assert
#endif

FLEXFAT_LAYOUT_ASSERT((FLEXFAT_USER_ADDRESS_LIMIT >> FLEXFAT_REGION_SIZE_LOG) *
                              sizeof(uint64_t) <=
                          FLEXFAT_TABLES_OFFSET,
                      "FlexFat metadata table exceeds its fixed slot");
FLEXFAT_LAYOUT_ASSERT(FLEXFAT_REGION_BASE +
                              FLEXFAT_NUM_SIZE_CLASSES * FLEXFAT_REGION_SIZE <=
                          FLEXFAT_TABLES_BASE,
                      "FlexFat managed regions overlap fixed metadata");
FLEXFAT_LAYOUT_ASSERT(FLEXFAT_TABLES_BASE + FLEXFAT_TABLES_SPAN <=
                          FLEXFAT_USER_ADDRESS_LIMIT,
                      "FlexFat fixed metadata exceeds the user address range");
FLEXFAT_LAYOUT_ASSERT(FLEXFAT_MAX_SIZE <= FLEXFAT_REGION_SIZE / 4,
                      "FlexFat largest class exceeds one quarter of a region");
#ifndef FLEXFAT_CUSTOM_CONFIG
FLEXFAT_LAYOUT_ASSERT(FLEXFAT_TEMPORAL_SHADOW_OFFSET +
                              FLEXFAT_DIRECT_SHADOW_BYTES <=
                          FLEXFAT_USER_ADDRESS_LIMIT,
                      "FlexFat direct shadow exceeds the user address range");
#endif

#undef FLEXFAT_LAYOUT_ASSERT

#endif // FLEXFAT_LAYOUT_H
