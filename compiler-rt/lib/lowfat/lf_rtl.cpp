//===-- lf_rtl.cpp - LowFat Sanitizer Runtime Library ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Runtime library for the LowFat pointer bounds-checking sanitizer.
//
// Manages size-class-aligned memory regions so that allocation bounds can be
// recovered from any heap pointer in O(1) using only bit operations:
//   region_index = (ptr - kRegionBase) >> kRegionSizeLog
//   size         = sizes_table[region_index]
//   base         = ptr & masks_table[region_index]
//
//===----------------------------------------------------------------------===//

#include "lf_allocator.h"
#include "lf_config.h"
#include "lf_interface.h"
#include "lf_stack.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include <stddef.h>

using namespace __sanitizer;

namespace __lowfat {

// Shared state visible to lf_interceptors.cpp.
bool lowfat_inited = false;
bool lowfat_recover = false;
bool lowfat_right_align = false;

static constexpr uptr kMallocAlignment = alignof(max_align_t);

// Region bookkeeping.
RegionInfo kRegions[kNumSizeClasses];
static uptr region_bases[kNumSizeClasses];
static uptr region_next_alloc[kNumSizeClasses];

// Segregated free lists: freed blocks store a next pointer at their start.
struct FreeBlock {
  FreeBlock *next;
};
static FreeBlock *free_lists[kNumSizeClasses];

// Per-size-class spinlocks to allow concurrent allocation across classes.
static StaticSpinMutex region_locks[kNumSizeClasses];

// Fixed-address metadata tables used by the instrumentation pass.
// The pass emits absolute-addressed loads from these tables so that the
// backend can fold them into a single mov instruction.
//
//   kTablesBase + 0 * kTablesOffset : sizes  (u64 per class)
//   kTablesBase + 3 * kTablesOffset : masks  (u64 per class)
static constexpr uptr kTablesBase   = 0x118000000000ULL;
static constexpr uptr kTablesOffset = 0x1000000ULL;  // 16 MB between tables

//===----------------------------------------------------------------------===//
// Initialization
//===----------------------------------------------------------------------===//

static void InitializeFlags() {
  SetCommonFlagsDefaults();

  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.exitcode = 1;
    cf.abort_on_error = false;
    OverrideCommonFlags(cf);
  }

  FlagParser parser;
  RegisterCommonFlags(&parser);
  parser.ParseStringFromEnv("LOWFAT_OPTIONS");

  InitializeCommonFlags();
}

static void InitTables() {
  // Reserve 64 MB at the fixed address for metadata tables.
  if (!MmapFixedNoReserve(kTablesBase, 64 * 1024 * 1024, "lowfat_tables"))
    Die();

  u64 *sizes = (u64 *)(kTablesBase + 0 * kTablesOffset);
  u64 *masks = (u64 *)(kTablesBase + 3 * kTablesOffset);

  // Fill valid classes, zero the rest (zero size → non-LowFat guard in pass).
  for (uptr i = 0; i < 1024; i++) {
    if (i < kNumSizeClasses) {
      u64 size = (u64)SizeClassToSize(i);
      sizes[i] = size;
      masks[i] = ~(size - 1);
    } else {
      sizes[i] = 0;
      masks[i] = 0;
    }
  }
}

static void InitRegionTable() {
  for (uptr i = 0; i < kNumSizeClasses; i++) {
    uptr size = SizeClassToSize(i);
    kRegions[i].size      = size;
    kRegions[i].alignment = size;
    kRegions[i].mask      = ~(size - 1);
    free_lists[i] = nullptr;
  }
}

static bool InitMemoryRegions() {
  for (uptr i = 0; i < kNumSizeClasses; i++) {
    uptr region_start = GetRegionStart(i);

    if (!MmapFixedNoReserve(region_start, kRegionSize, "lowfat_region"))
      return false;

    region_bases[i] = region_start;

    // First allocation must be aligned to the size class relative to
    // absolute zero so that base recovery via AND works correctly.
    uptr size = kRegions[i].size;
    uptr offset = region_start % size;
    uptr initial_alloc = region_start;
    if (offset != 0)
      initial_alloc += (size - offset);

    region_next_alloc[i] = initial_alloc;
  }
  return true;
}

//===----------------------------------------------------------------------===//
// Allocator
//===----------------------------------------------------------------------===//

void *Allocate(uptr size) {
  if (size == 0)
    size = 1;

  uptr class_index = SizeClassIndex(size);
  if (class_index >= kNumSizeClasses)
    return nullptr;

  uptr alloc_size = SizeClassToSize(class_index);

  SpinMutexLock lock(&region_locks[class_index]);

  uptr slot_base;

  // Try free list first.
  FreeBlock *block = free_lists[class_index];
  if (block) {
    free_lists[class_index] = block->next;
    slot_base = (uptr)block;
    internal_memset(block, 0, alloc_size);
  } else {
    // Bump allocation.
    uptr region_end = GetRegionStart(class_index) + kRegionSize;
    uptr addr = region_next_alloc[class_index];

    if (addr + alloc_size > region_end) {
      // Region exhausted — fall back to system allocator.
      return (void *)InternalAlloc(size);
    }

    region_next_alloc[class_index] = addr + alloc_size;
    slot_base = addr;
  }

  // In right-align mode, shift the returned pointer toward the slot end
  // while preserving malloc alignment.
  if (lowfat_right_align) {
    uptr slack = alloc_size - size;
    uptr aligned_offset = RoundDownTo(slack, kMallocAlignment);
    return (void *)(slot_base + aligned_offset);
  }
  return (void *)slot_base;
}

void Deallocate(void *ptr) {
  if (!ptr)
    return;

  uptr addr = (uptr)ptr;

  if (!IsLowFatPointer(addr)) {
    InternalFree(ptr);
    return;
  }

  uptr region = GetRegionIndex(addr);
  // Always push the slot base (not the user pointer) so that the slot can
  // be reused with a different right-align offset.
  uptr slot_base = GetBase(addr);

  SpinMutexLock lock(&region_locks[region]);

  FreeBlock *block = (FreeBlock *)slot_base;
  block->next = free_lists[region];
  free_lists[region] = block;
}

//===----------------------------------------------------------------------===//
// Error Reporting
//===----------------------------------------------------------------------===//

static void PrintOobHeader(const char *level, uptr ptr, uptr base, uptr bound,
                           int is_write) {
  sptr overflow = (sptr)(ptr) - (sptr)(base + bound);
  const char *op = is_write ? "write" : "read";

  Printf("LOWFAT %s: out-of-bounds error detected!\n", level);
  Printf("          operation = %s\n", op);
  Printf("          pointer   = 0x%zx (heap)\n", ptr);
  Printf("          base      = 0x%zx\n", base);
  Printf("          size      = %zu\n", bound);
  const char *sign = (overflow >= 0) ? "+" : "";
  Printf("          overflow  = %s%zd\n", sign, (long)overflow);
  Printf("\n");
}

static void PrintErrorAndDie(uptr ptr, uptr base, uptr bound, int is_write,
                             const StackTrace &stack) {
  PrintOobHeader("ERROR", ptr, base, bound, is_write);
  stack.Print();
  Die();
}

static void PrintWarning(uptr ptr, uptr base, uptr bound, int is_write,
                         const StackTrace &stack) {
  PrintOobHeader("WARNING", ptr, base, bound, is_write);
  stack.Print();
}

}  // namespace __lowfat

//===----------------------------------------------------------------------===//
// Interface Functions
//===----------------------------------------------------------------------===//

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE
void __lf_set_recover(int recover) {
  __lowfat::lowfat_recover = (recover != 0);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lf_set_right_align(int right_align) {
  __lowfat::lowfat_right_align = (right_align != 0);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lf_init() {
  if (__lowfat::lowfat_inited)
    return;

  __lowfat::InitializeFlags();
  __lowfat::InitTables();
  __lowfat::InitRegionTable();

  if (!__lowfat::InitMemoryRegions())
    Die();

  __lowfat::lowfat_inited = true;
  __lowfat::InitializeInterceptors();
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lf_report_oob(uptr ptr, uptr base, uptr bound, int is_write) {
  GET_STACK_TRACE_FATAL_HERE;
  __lowfat::PrintErrorAndDie(ptr, base, bound, is_write, stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lf_warn_oob(uptr ptr, uptr base, uptr bound, int is_write) {
  GET_STACK_TRACE_FATAL_HERE;
  __lowfat::PrintWarning(ptr, base, bound, is_write, stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __lf_get_base(uptr ptr) {
  return __lowfat::GetBase(ptr);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __lf_get_size(uptr ptr) {
  return __lowfat::GetSize(ptr);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __lf_get_offset(uptr ptr) {
  uptr base = __lowfat::GetBase(ptr);
  if (base == 0)
    return 0;
  return ptr - base;
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __lf_get_usable_size(uptr ptr) {
  uptr base = __lowfat::GetBase(ptr);
  uptr size = __lowfat::GetSize(ptr);
  if (base == 0)
    return (uptr)-1;
  return size - (ptr - base);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__lf_malloc(uptr size) {
  return __lowfat::Allocate(size);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __lf_free(void *ptr) {
  __lowfat::Deallocate(ptr);
}

}  // extern "C"

#if SANITIZER_CAN_USE_PREINIT_ARRAY
__attribute__((section(".preinit_array"), used)) static auto preinit =
    __lf_init;
#else
__attribute__((constructor)) static void lowfat_constructor() {
  __lf_init();
}
#endif
