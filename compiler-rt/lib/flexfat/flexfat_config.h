//===-- flexfat_config.h - FlexFat Memory Layout Configuration
//-----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the FlexFat memory layout configuration.
//
// FlexFat pointers encode allocation bounds directly in the pointer value:
// - Memory is divided into regions, each for a specific size class
// - Within each region, allocations are aligned to their size class
// - Given a pointer, the base can be computed either by masking off low bits
//   (POW2-only mode) or via fixed-point magic-number math (custom mode)
// - The size can be looked up from a table using the region index
//
// Default Memory Layout (POW2-only mode, kRegionSizeLog=32):
//   Region 0: [0x10_0000_0000, 0x20_0000_0000) - 16-byte allocations
//   Region 1: [0x20_0000_0000, 0x30_0000_0000) - 32-byte allocations
//   Region 2: [0x30_0000_0000, 0x40_0000_0000) - 64-byte allocations
//   ...
//   Region N: [0xN0_0000_0000, ...)            - 2^(N+4)-byte allocations
//
// Custom Config Mode (FLEXFAT_CUSTOM_CONFIG, kRegionSizeLog=38):
//   Arbitrary configured sizes (e.g. 48, 80, 96 bytes) are also supported.
//   kRegionSizeLog increases to 38 (256 GiB per region), allowing all 64
//   LowFat source classes through 64 GiB.
//   The key helpers (SizeClassIndex, SizeClassToSize) switch to table lookups.
//   Base recovery uses generated reciprocal tables for every size class.
//
//===----------------------------------------------------------------------===//

#ifndef LF_CONFIG_H
#define LF_CONFIG_H

#include "sanitizer_common/sanitizer_internal_defs.h"

#ifdef FLEXFAT_CUSTOM_CONFIG
#include "flexfat_config_generated.h"
#endif

namespace __flexfat {

using namespace __sanitizer;

inline bool CheckBoundsImpl(uptr ptr, uptr access_size, uptr base,
                            uptr alloc_size) {
  uptr offset = ptr - base;
  if (access_size > alloc_size)
    return false;
  return offset <= alloc_size - access_size;
}

//===----------------------------------------------------------------------===//
// Size Class Configuration
//===----------------------------------------------------------------------===//

// Minimum allocation size (must be power of 2)
constexpr uptr kMinSizeLog = 4; // 16 bytes
constexpr uptr kMinSize = 1ULL << kMinSizeLog;

#ifdef FLEXFAT_CUSTOM_CONFIG

constexpr uptr kNumSizeClasses = FLEXFAT_NUM_SIZE_CLASSES;
constexpr uptr kMaxSize = FLEXFAT_MAX_SIZE;

// SizeClassIndex: table lookup (binary search on kFlexFatGenSizes[])
// Replaces the POW2-only __builtin_clzll math.
inline uptr SizeClassIndex(uptr size) {
  return (uptr)flexfat_size_to_class((uint64_t)size);
}

// SizeClassToSize: direct table lookup — works for arbitrary configured sizes.
inline uptr SizeClassToSize(uptr class_index) {
  if (class_index >= kNumSizeClasses)
    return 0;
  return (uptr)kFlexFatGenSizes[class_index];
}

#else

// Maximum allocation size (must be power of 2)
constexpr uptr kMaxSizeLog = 30; // 1 GB
constexpr uptr kMaxSize = 1ULL << kMaxSizeLog;

// Number of size classes (one per power of 2)
constexpr uptr kNumSizeClasses = kMaxSizeLog - kMinSizeLog + 1;

// Size class index for a given size (rounded up to next power of 2)
// Returns 0 for sizes <= 16, 1 for sizes 17-32, etc.
inline uptr SizeClassIndex(uptr size) {
  if (size <= kMinSize)
    return 0;
  // Count leading zeros to find the highest set bit
  uptr log2 = (sizeof(uptr) * 8 - 1) - __builtin_clzll(size);
  // Round up if not exact power of 2
  if (size > (1ULL << log2))
    log2++;
  return log2 - kMinSizeLog;
}

// Get the allocation size for a size class
inline uptr SizeClassToSize(uptr class_index) {
  return 1ULL << (class_index + kMinSizeLog);
}

#endif // FLEXFAT_CUSTOM_CONFIG

//===----------------------------------------------------------------------===//
// Memory Region Configuration
//===----------------------------------------------------------------------===//

#ifdef FLEXFAT_CUSTOM_CONFIG
constexpr uptr kRegionSizeLog = FLEXFAT_REGION_SIZE_LOG;
#else
// Each region is 4GB (32 bits of address space per region)
constexpr uptr kRegionSizeLog = 32;
#endif

constexpr uptr kRegionSize = 1ULL << kRegionSizeLog;
constexpr uptr kTablesOffset = 0x1000000ULL;
constexpr uptr kUserAddressLimit = 1ULL << 48;

// Base address where FlexFat regions start
// We use the upper portion of the address space
// On 64-bit systems: 0x100000000000 (17.6 TB mark)
#ifdef FLEXFAT_CUSTOM_CONFIG
constexpr uptr kRegionBase = FLEXFAT_REGION_BASE;
constexpr uptr kTablesBase = FLEXFAT_TABLES_BASE;
#else
constexpr uptr kRegionBase = 0x100000000000ULL;
constexpr uptr kTablesBase = 0x118000000000ULL;
#endif

// Get the region number from a pointer
inline uptr GetRegionIndex(uptr ptr) {
  if (ptr < kRegionBase)
    return (uptr)-1; // Not a FlexFat pointer
  return (ptr - kRegionBase) >> kRegionSizeLog;
}

// Get the start address of a region
inline uptr GetRegionStart(uptr region_index) {
  return kRegionBase + (region_index << kRegionSizeLog);
}

// Check if a pointer is within FlexFat managed memory
inline bool IsFlexFatPointer(uptr ptr) {
  uptr region = GetRegionIndex(ptr);
  return region < kNumSizeClasses;
}

inline uptr GetTableIndex(uptr ptr) {
  return ptr < kUserAddressLimit ? ptr >> kRegionSizeLog : 0;
}

//===----------------------------------------------------------------------===//
// Bounds Computation
//===----------------------------------------------------------------------===//

// Get the allocation size from a FlexFat pointer
inline uptr GetSize(uptr ptr) {
  const uptr *sizes = (const uptr *)(kTablesBase + 0 * kTablesOffset);
  return sizes[GetTableIndex(ptr)];
}

#ifdef FLEXFAT_CUSTOM_CONFIG

// GetBase override for custom config: use reciprocal fixed-point
// multiplication for every configured size class, including power-of-two
// classes. This keeps base recovery uniform across custom layouts.
//
//   quotient = (u128)ptr * magic >> 64
//   if (quotient * size > ptr) --quotient
//   base = quotient * size
inline uptr GetBase(uptr ptr) {
  uptr table_index = GetTableIndex(ptr);
  const uptr *sizes = (const uptr *)(kTablesBase + 0 * kTablesOffset);
  const uptr *magics = (const uptr *)(kTablesBase + 1 * kTablesOffset);
  typedef unsigned __int128 u128;
  u128 mul = (u128)ptr * (u128)magics[table_index];
  uptr idx = (uptr)(mul >> 64);
  uptr size = sizes[table_index];
  if ((u128)idx * size > ptr)
    --idx;
  return idx * size;
}

// CheckBounds override: uses the custom GetBase above.
inline bool CheckBounds(uptr ptr, uptr access_size) {
  uptr region = GetRegionIndex(ptr);
  if (region >= kNumSizeClasses)
    return true; // Not a FlexFat pointer — assume valid
  uptr alloc_size = SizeClassToSize(region);
  uptr base = GetBase(ptr);
  return CheckBoundsImpl(ptr, access_size, base, alloc_size);
}

#else

// Get the base address of an allocation from a FlexFat pointer
// This uses the key FlexFat insight: allocations are aligned to their size
inline uptr GetBase(uptr ptr) {
  uptr table_index = GetTableIndex(ptr);
  const uptr *masks = (const uptr *)(kTablesBase + 3 * kTablesOffset);
  return ptr & masks[table_index];
}

// Check if ptr..ptr+access_size is within bounds
inline bool CheckBounds(uptr ptr, uptr access_size) {
  uptr region = GetRegionIndex(ptr);
  if (region >= kNumSizeClasses)
    return true; // Not a FlexFat pointer, assume valid (or could error)

  uptr alloc_size = SizeClassToSize(region);
  uptr base = ptr & ~(alloc_size - 1);

  return CheckBoundsImpl(ptr, access_size, base, alloc_size);
}

#endif // FLEXFAT_CUSTOM_CONFIG

//===----------------------------------------------------------------------===//
// Region Table (for lookup by region index)
//===----------------------------------------------------------------------===//

struct RegionInfo {
  uptr size;      // Allocation size for this region
  uptr alignment; // Alignment (same as size for FlexFat)
};

// This table is indexed by region number
// Initialized in flexfat_rtl.cpp
extern RegionInfo kRegions[kNumSizeClasses];

} // namespace __flexfat

#endif // LF_CONFIG_H
