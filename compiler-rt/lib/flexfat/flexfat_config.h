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
// The compiler, runtime, and generator share the address-space definitions in
// flexfat_layout.h. Custom builds additionally generate the size-class profile.
//
//===----------------------------------------------------------------------===//

#ifndef LF_CONFIG_H
#define LF_CONFIG_H

#if defined(FLEXFAT_TBI_PACKED_SHADOW) && defined(FLEXFAT_CUSTOM_CONFIG)
#error "FlexFat TBI packed-shadow storage requires a POW2 build"
#endif


#include "sanitizer_common/sanitizer_internal_defs.h"

#include "flexfat_layout.h"

namespace __flexfat {

using namespace __sanitizer;

// Application pointers retain their top byte; only runtime arithmetic is raw.
inline uptr Untag(uptr ptr) {
#ifdef FLEXFAT_TEMPORAL_TBI
  return ptr & 0x00ffffffffffffffULL;
#else
  return ptr;
#endif
}
inline unsigned PointerTag(uptr ptr) { return ptr >> 56; }
inline uptr TagPointer(uptr ptr, unsigned tag) {
  return Untag(ptr) | (uptr(tag) << 56);
}

inline bool CheckBoundsImpl(uptr ptr, uptr access_size, uptr base,
                            uptr alloc_size) {
  // Every allocation reserves its trailing byte, including spatial-only and
  // shadow-tag modes. Legal one-past pointers may point here, accesses may not.
  --alloc_size;
  uptr offset = ptr - base;
  if (access_size > alloc_size)
    return false;
  return offset <= alloc_size - access_size;
}

inline uptr UsableClassSize(uptr size) {
  return size - 1;
}

//===----------------------------------------------------------------------===//
// Size Class Configuration
//===----------------------------------------------------------------------===//

// Minimum allocation size (must be power of 2)
constexpr uptr kMinSizeLog = FLEXFAT_MIN_SIZE_LOG;
constexpr uptr kMinSize = 1ULL << kMinSizeLog;

constexpr uptr kNumSizeClasses = FLEXFAT_NUM_SIZE_CLASSES;
constexpr uptr kMaxSize = FLEXFAT_MAX_SIZE;

#ifdef FLEXFAT_CUSTOM_CONFIG

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
constexpr uptr kMaxSizeLog = FLEXFAT_POW2_MAX_SIZE_LOG;

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

constexpr uptr kRegionSizeLog = FLEXFAT_REGION_SIZE_LOG;
constexpr uptr kRegionSize = FLEXFAT_REGION_SIZE;
constexpr uptr kTablesOffset = FLEXFAT_TABLES_OFFSET;
constexpr uptr kUserAddressLimit = FLEXFAT_USER_ADDRESS_LIMIT;
constexpr uptr kRegionBase = FLEXFAT_REGION_BASE;
constexpr uptr kTablesBase = FLEXFAT_TABLES_BASE;

// Get the region number from a pointer
inline uptr GetRegionIndex(uptr ptr) {
  ptr = Untag(ptr);
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
  ptr = Untag(ptr);
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

// Recover a managed slot base when its size is already known. In particular,
// POW2 bounds checks need only a mask, without another metadata-table lookup.
inline uptr GetBaseForSize(uptr ptr, uptr size) {
  ptr = Untag(ptr);
#ifdef FLEXFAT_CUSTOM_CONFIG
  // Use the corrected reciprocal for every configured class, including powers
  // of two, so allocation geometry and instrumentation use the same recovery.
  const uptr *magics = (const uptr *)(kTablesBase + kTablesOffset);
  typedef unsigned __int128 u128;
  uptr quotient = ((u128)ptr * magics[GetTableIndex(ptr)]) >> 64;
  uptr candidate = quotient * size;
  return candidate > ptr ? candidate - size : candidate;
#else
  return ptr & ~(size - 1);
#endif
}

// Foreign pointers recover base zero through their zero magic or mask entry.
inline uptr GetBase(uptr ptr) {
#ifdef FLEXFAT_CUSTOM_CONFIG
  return GetBaseForSize(ptr, GetSize(ptr));
#else
  ptr = Untag(ptr);
  const uptr *masks = (const uptr *)(kTablesBase + 3 * kTablesOffset);
  return ptr & masks[GetTableIndex(ptr)];
#endif
}

// Check whether the complete access fits the usable managed slot. Foreign
// memory remains permissive; both layouts use the same access predicate.
inline bool CheckBounds(uptr ptr, uptr access_size) {
  ptr = Untag(ptr);
  uptr region = GetRegionIndex(ptr);
  if (region >= kNumSizeClasses)
    return true;
  uptr size = SizeClassToSize(region);
  return CheckBoundsImpl(ptr, access_size, GetBaseForSize(ptr, size), size);
}

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
