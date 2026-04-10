//===-- lf_config.h - LowFat Memory Layout Configuration -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// LowFat pointers encode allocation bounds directly in the pointer value:
// - Memory is divided into regions, each for a specific power-of-two size class
// - Within each region, allocations are aligned to their size class
// - Given a pointer, the base is computed by masking off low bits:
//     base = ptr & ~(size - 1)
// - The size is looked up from a table indexed by region number
//
// Memory Layout (kRegionSizeLog=32, 4GB per region):
//   Region 0: [0x100_0000_0000, 0x110_0000_0000) - 16-byte allocations
//   Region 1: [0x110_0000_0000, 0x120_0000_0000) - 32-byte allocations
//   ...
//   Region 26: [0x11A_0000_0000, ...)            - 2^30-byte allocations
//
//===----------------------------------------------------------------------===//

#ifndef LF_CONFIG_H
#define LF_CONFIG_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __lowfat {

using namespace __sanitizer;

//===----------------------------------------------------------------------===//
// Size Class Configuration (power-of-two only)
//===----------------------------------------------------------------------===//

constexpr uptr kMinSizeLog = 4;   // 16 bytes
constexpr uptr kMinSize = 1ULL << kMinSizeLog;

constexpr uptr kMaxSizeLog = 30;  // 1 GB
constexpr uptr kMaxSize = 1ULL << kMaxSizeLog;

// One size class per power of two: 16, 32, 64, ..., 1G
constexpr uptr kNumSizeClasses = kMaxSizeLog - kMinSizeLog + 1;  // 27

// Size class index for a given allocation size (rounded up to next POW2).
// Returns 0 for sizes <= 16, 1 for 17-32, etc.
inline uptr SizeClassIndex(uptr size) {
  if (size <= kMinSize)
    return 0;
  uptr log2 = (sizeof(uptr) * 8 - 1) - __builtin_clzll(size);
  if (size > (1ULL << log2))
    log2++;
  return log2 - kMinSizeLog;
}

// Allocation size for a given size class index.
inline uptr SizeClassToSize(uptr class_index) {
  return 1ULL << (class_index + kMinSizeLog);
}

//===----------------------------------------------------------------------===//
// Memory Region Configuration
//===----------------------------------------------------------------------===//

// Each region is 4GB of virtual address space.
constexpr uptr kRegionSizeLog = 32;
constexpr uptr kRegionSize = 1ULL << kRegionSizeLog;

// Regions start at the 17.6TB mark in the virtual address space.
constexpr uptr kRegionBase = 0x100000000000ULL;

// Region index from a pointer value.
inline uptr GetRegionIndex(uptr ptr) {
  if (ptr < kRegionBase)
    return (uptr)-1;
  return (ptr - kRegionBase) >> kRegionSizeLog;
}

// Start address of a given region.
inline uptr GetRegionStart(uptr region_index) {
  return kRegionBase + (region_index << kRegionSizeLog);
}

// True if the pointer falls within a valid LowFat region.
inline bool IsLowFatPointer(uptr ptr) {
  uptr region = GetRegionIndex(ptr);
  return region < kNumSizeClasses;
}

//===----------------------------------------------------------------------===//
// Bounds Computation
//===----------------------------------------------------------------------===//

// Allocation size from a LowFat pointer (derived from its region).
inline uptr GetSize(uptr ptr) {
  uptr region = GetRegionIndex(ptr);
  if (region >= kNumSizeClasses)
    return (uptr)-1;
  return SizeClassToSize(region);
}

// Base address of the allocation slot containing ptr.
// Uses the key LowFat insight: POW2-aligned allocations allow
// base recovery with a single AND: base = ptr & ~(size - 1).
inline uptr GetBase(uptr ptr) {
  uptr region = GetRegionIndex(ptr);
  if (region >= kNumSizeClasses)
    return 0;
  uptr size = SizeClassToSize(region);
  return ptr & ~(size - 1);
}

// Check if ptr..ptr+access_size is within the allocation bounds.
inline bool CheckBounds(uptr ptr, uptr access_size) {
  uptr region = GetRegionIndex(ptr);
  if (region >= kNumSizeClasses)
    return true;  // Not a LowFat pointer — assume valid
  uptr alloc_size = SizeClassToSize(region);
  uptr base = ptr & ~(alloc_size - 1);
  return (ptr + access_size) <= (base + alloc_size);
}

//===----------------------------------------------------------------------===//
// Region Table
//===----------------------------------------------------------------------===//

struct RegionInfo {
  uptr size;
  uptr alignment;
  uptr mask;  // ~(size - 1)
};

extern RegionInfo kRegions[kNumSizeClasses];

}  // namespace __lowfat

#endif  // LF_CONFIG_H
