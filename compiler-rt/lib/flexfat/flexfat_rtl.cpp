//===-- flexfat_rtl.cpp - FlexFat Sanitizer Runtime Library
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is the main file of the FlexFat Sanitizer runtime library.
//
// FlexFat pointers encode allocation bounds information directly in the pointer
// value through careful memory layout. This allows O(1) bounds checking without
// maintaining separate metadata.
//
//===----------------------------------------------------------------------===//

#include "flexfat_allocator.h"
#include "flexfat_config.h"
#include "flexfat_interface.h"
#include "flexfat_stack.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include <stddef.h>
#include "sanitizer_common/sanitizer_atomic.h"
#if defined(FLEXFAT_EXACT_DEALLOCATION) && !defined(FLEXFAT_TEMPORAL_TBI)
#error "Exact deallocation requires temporal support"
#endif

#ifdef FLEXFAT_TEMPORAL_TBI
#if !defined(__aarch64__) || !defined(__linux__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__ || __SIZEOF_POINTER__ != 8
#error FlexFat TBI requires little-endian Linux AArch64 with 64-bit pointers
#endif
#include "sanitizer_common/sanitizer_linux.h"
#include "sanitizer_common/sanitizer_libc.h"
#include <linux/prctl.h>
#endif

using namespace __sanitizer;

namespace __flexfat {

// Initialization is published only after mappings and interceptors are ready.
static atomic_uint32_t init_state;
bool IsReady() {
  return atomic_load(&init_state, memory_order_acquire) == unsigned(InitState::Ready);
}

// Set to true when -fsanitize-recover=flexfat is active. Controls whether
// interceptor-level OOB (memset/memcpy/memmove) warns-and-continues or aborts.
bool flexfat_recover = false;

// Set to true when -flexfat-mode=right-align is active. Instructs Allocate()
// to bias objects toward the high end of their size-class slot while still
// preserving the default malloc alignment guarantee. This can improve detection
// of some right-side overflows, but the object's right edge does not always
// coincide exactly with the slot boundary once alignment is enforced.
bool flexfat_right_align = false;

// malloc() must return a pointer suitably aligned for any object type.
static constexpr uptr kMallocAlignment = alignof(max_align_t);

// Maximum number of size classes across both modes.
// In POW2-only mode kNumSizeClasses=27; with a custom config it can be up to
// FLEXFAT_MAX_ARRAY_SIZE. We size the static arrays at compile time using
// whichever is larger so the same translation unit works in both modes.
#ifdef FLEXFAT_CUSTOM_CONFIG
static constexpr uptr kMaxSizeClasses = FLEXFAT_NUM_SIZE_CLASSES;
#else
static constexpr uptr kMaxSizeClasses = kNumSizeClasses;
#endif

// Region table - initialized in __flexfat_init
RegionInfo kRegions[kMaxSizeClasses];

// Pointers to the start of each mapped region
static uptr region_bases[kMaxSizeClasses];

// Bump pointer: next fresh address to allocate from in each region
static uptr region_next_alloc[kMaxSizeClasses];

// Segregated free lists: one singly-linked list per size class
// Free blocks store a pointer to the next free block at their start
struct FreeBlock {
  FreeBlock *next;
};
static FreeBlock *free_lists[kMaxSizeClasses];

// Per-size-class spin mutexes protecting region_next_alloc and free_lists.
// Using one lock per size class allows concurrent allocation across different
// size classes, which is the common case in multi-threaded programs.
static StaticSpinMutex region_locks[kMaxSizeClasses];

#ifdef FLEXFAT_TEMPORAL_TBI
struct TemporalRegion {
  uptr first;
  uptr count;
  atomic_uint16_t *entries;
#ifdef FLEXFAT_EXACT_DEALLOCATION
  // Accessed only under the corresponding region_locks entry.
  u64 *user_offsets;
#endif
};
static TemporalRegion temporal_regions[kMaxSizeClasses];

static void InitTemporal() {
  uptr slots = 0;
  for (uptr i = 0; i < kNumSizeClasses; ++i) {
    auto &r = temporal_regions[i];
    r.first = region_next_alloc[i];
    r.count = (GetRegionStart(i) + kRegionSize - r.first) / SizeClassToSize(i);
    slots += r.count;
  }
  // Demand-paged, non-fixed mapping, after every fixed spatial reservation.
  auto *entries = static_cast<atomic_uint16_t *>(
      MmapNoReserveOrDie(slots * sizeof(atomic_uint16_t),
                         "flexfat temporal metadata initialization"));
#ifdef FLEXFAT_EXACT_DEALLOCATION
  // A separate mapping keeps every u64 aligned even when a region has an odd
  // number of temporal entries. Neither demand-paged mapping is eagerly cleared.
  auto *offsets = static_cast<u64 *>(
      MmapNoReserveOrDie(slots * sizeof(u64),
                         "flexfat allocation offset metadata initialization"));
#endif
  for (uptr i = 0; i < kNumSizeClasses; ++i) {
    temporal_regions[i].entries = entries;
#ifdef FLEXFAT_EXACT_DEALLOCATION
    temporal_regions[i].user_offsets = offsets;
#endif
    entries += temporal_regions[i].count;
#ifdef FLEXFAT_EXACT_DEALLOCATION
    offsets += temporal_regions[i].count;
#endif
  }
}

static atomic_uint16_t *TemporalEntry(uptr raw, uptr &base) {
  uptr region = GetRegionIndex(raw);
  if (region >= kNumSizeClasses)
    return nullptr;
  uptr size = SizeClassToSize(region);
  base = raw - raw % size;
  const auto &r = temporal_regions[region];
  if (base < r.first || (base - r.first) % size ||
      (base - r.first) / size >= r.count)
    return nullptr;
  return r.entries + (base - r.first) / size;
}

struct TemporalState {
  uptr base = 0;
  atomic_uint16_t *entry = nullptr;
  unsigned bits = 0;

  bool Matches(uptr ptr) const {
    unsigned tag = PointerTag(ptr);
    return entry && tag && (bits & 256) && (bits & 255) == tag;
  }
};

static TemporalState LoadTemporalState(uptr ptr) {
  TemporalState state;
  state.entry = TemporalEntry(Untag(ptr), state.base);
  if (state.entry)
    state.bits = atomic_load(state.entry, memory_order_acquire);
  return state;
}

#ifdef FLEXFAT_EXACT_DEALLOCATION
// The caller must hold the class lock and have validated the entry's geometry.
// Keep offset lookups separate from ordinary read/write temporal validation.
static u64 &UserOffset(uptr region, atomic_uint16_t *entry) {
  const auto &r = temporal_regions[region];
  return r.user_offsets[entry - r.entries];
}
#endif

// Reporting may allocate while symbolizing. Never hold an allocator lock here.
static void NORETURN ReportTemporal(uptr ptr, uptr access_size, int operation,
                                    const TemporalState &state) {
  const char *ops[] = {"read", "write", "free", "realloc"};
  const char *reason = !state.entry ? "invalid slot geometry"
                       : !state.bits ? "never allocated"
                       : !PointerTag(ptr) ? "zero managed tag"
                       : !(state.bits & 256) ? "dead slot"
                       : "generation mismatch";
  Printf("FLEXFAT ERROR: temporal violation\n"
         "  operation = %s, tagged address = 0x%zx, raw slot base = 0x%zx\n"
         "  current generation = %u, pointer tag = %u, live = %u, access size = %zu\n"
         "  metadata = %s, reason = %s\n",
         operation >= 0 && operation < 4 ? ops[operation] : "unknown",
         ptr, state.base, state.bits & 255, PointerTag(ptr),
         !!(state.bits & 256), access_size,
         state.entry ? "available" : "unavailable (invalid slot geometry)",
         reason);
  GET_STACK_TRACE_FATAL_HERE;
  stack.Print();
  Die();
}

#ifdef FLEXFAT_EXACT_DEALLOCATION
static void NORETURN ReportInvalidDeallocation(uptr ptr, int operation,
                                              uptr expected, uptr base) {
  Printf("FLEXFAT ERROR: invalid deallocation\n"
         "  operation = %s, tagged address = 0x%zx\n"
         "  supplied address = 0x%zx, expected allocation address = 0x%zx, slot base = 0x%zx\n"
         "  reason = not allocation start\n",
         operation == 2 ? "free" : "realloc", ptr, Untag(ptr), expected, base);
  GET_STACK_TRACE_FATAL_HERE;
  stack.Print();
  Die();
}
#endif

static void ValidateTemporal(uptr ptr, uptr access_size, int operation) {
  if ((operation < 2 && !access_size) || !IsFlexFatPointer(ptr))
    return;
#ifdef FLEXFAT_EXACT_DEALLOCATION
  if (operation == 3) {
    uptr region = GetRegionIndex(Untag(ptr));
    TemporalState state;
    uptr expected = 0;
    bool temporal_valid;
    bool exact_valid;
    {
      SpinMutexLock lock(&region_locks[region]);
      state = LoadTemporalState(ptr);
      temporal_valid = state.Matches(ptr);
      if (temporal_valid)
        expected = state.base + UserOffset(region, state.entry);
      exact_valid = temporal_valid && Untag(ptr) == expected;
    }
    if (!temporal_valid)
      ReportTemporal(ptr, access_size, operation, state);
    if (!exact_valid)
      ReportInvalidDeallocation(ptr, operation, expected, state.base);
    return;
  }
#endif
  TemporalState state = LoadTemporalState(ptr);
  if (!state.Matches(ptr))
    ReportTemporal(ptr, access_size, operation, state);
}

static void EnableTaggedAddresses() {
  uptr result = internal_prctl(PR_SET_TAGGED_ADDR_CTRL, PR_TAGGED_ADDR_ENABLE, 0, 0, 0);
  int error = 0;
  if (internal_iserror(result, &error)) {
    Printf("FLEXFAT initialization failed: PR_SET_TAGGED_ADDR_CTRL, errno=%d\n", error);
    Die();
  }
  result = internal_prctl(PR_GET_TAGGED_ADDR_CTRL, 0, 0, 0, 0);
  if (internal_iserror(result, &error) || !(result & PR_TAGGED_ADDR_ENABLE)) {
    Printf("FLEXFAT initialization failed: PR_GET_TAGGED_ADDR_CTRL, errno=%d\n", error);
    Die();
  }
}
#endif

// Fixed address where the metadata tables are mapped during initialization.
// Pow2 mode uses the size and mask tables. Custom mode uses the size and magic
// tables. This allows the LLVM pass to use absolute addressing (imm[index*8])
// instead of PC-relative loads.
//
//   kTablesBase + 0 * kTablesOffset: Sizes (8 bytes per class)
//   kTablesBase + 1 * kTablesOffset: Magics (custom mode)
//   kTablesBase + 3 * kTablesOffset: Masks (POW2 mode)
// Custom mode generates kTablesBase together with its region geometry.
static constexpr uptr kTablesMappingSize = 4 * kTablesOffset;
static constexpr uptr kTableEntries = kUserAddressLimit >> kRegionSizeLog;
static constexpr uptr kManagedTableBegin = kRegionBase >> kRegionSizeLog;

static_assert(kTableEntries * sizeof(u64) <= kTablesOffset,
              "FlexFat metadata table exceeds its fixed mapping");
static_assert(kManagedTableBegin + kNumSizeClasses <= kTableEntries,
              "FlexFat managed regions exceed the 48-bit metadata table");
static_assert(kRegionBase + kNumSizeClasses * kRegionSize <= kTablesBase,
              "FlexFat managed regions overlap fixed metadata");
static_assert(kTablesBase + kTablesMappingSize <= kUserAddressLimit,
              "FlexFat fixed metadata exceeds the 48-bit address space");

static void InitializeFlags() {
  SetCommonFlagsDefaults();

  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.exitcode = 1;           // Fatal OOB exits with code 1 by default
    cf.abort_on_error = false; // Use the exitcode path, not SIGABRT, so output
                               // is flushed before the process exits
    OverrideCommonFlags(cf);
  }

  // Register all common flags with a parser and read FLEXFAT_OPTIONS.
  //    Allow overriding flags at runtime, e.g.:
  //    FLEXFAT_OPTIONS=exitcode=42:verbosity=1 ./my_program
  FlagParser parser;
  RegisterCommonFlags(&parser);
  parser.ParseStringFromEnv("FLEXFAT_OPTIONS");

  InitializeCommonFlags();
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
}

static void InitTables() {
  // Reserve enough address space for the fixed table offsets used by the pass.
  if (!MmapFixedNoReserve(kTablesBase, kTablesMappingSize, "flexfat_tables")) {
    Printf("FLEXFAT initialization failed: fixed spatial tables\n");
    Die();
  }

  u64 *sizes = (u64 *)(kTablesBase + 0 * kTablesOffset);
#ifdef FLEXFAT_CUSTOM_CONFIG
  u64 *magics = (u64 *)(kTablesBase + 1 * kTablesOffset);
#else
  u64 *masks = (u64 *)(kTablesBase + 3 * kTablesOffset);
#endif

  // LowFat-style absolute indexing covers every user address below 2^48.
  // Foreign regions receive wide bounds and zero recovery metadata; managed
  // regions overwrite those sentinels with their real class metadata.
  for (uptr i = 0; i < kTableEntries; ++i) {
    sizes[i] = ~(u64)0;
#ifdef FLEXFAT_CUSTOM_CONFIG
    magics[i] = 0;
#else
    masks[i] = 0;
#endif
  }

  for (uptr i = 0; i < kNumSizeClasses; ++i) {
    uptr table_index = kManagedTableBegin + i;
#ifdef FLEXFAT_CUSTOM_CONFIG
    sizes[table_index] = (u64)kFlexFatGenSizes[i];
    magics[table_index] = (u64)kFlexFatGenMagics[i];
#else
    u64 size = (u64)SizeClassToSize(i);
    sizes[table_index] = size;
    masks[table_index] = ~(size - 1);
#endif
  }
}

static void InitRegionTable() {
  for (uptr i = 0; i < kNumSizeClasses; i++) {
    uptr size = SizeClassToSize(i);
    kRegions[i].size = size;
    kRegions[i].alignment = size;
    free_lists[i] = nullptr;
  }
}

// Initialize memory regions using mmap
// Each region is mapped at a fixed address for the corresponding size class
static bool InitMemoryRegions() {
  for (uptr i = 0; i < kNumSizeClasses; i++) {
    uptr region_start = GetRegionStart(i);

    // Reserve the region without committing physical memory
    // MmapFixedNoReserve maps memory but doesn't allocate physical pages until
    // they're accessed
    bool success =
        MmapFixedNoReserve(region_start, kRegionSize, "flexfat_region");

    if (!success)
      return false;

    region_bases[i] = region_start;

    // The first allocation must be aligned to the object size relative to
    // absolute zero. This is required for the magic-number fixed point math to
    // securely compute object bases.
    uptr size = kRegions[i].size;
    uptr offset = region_start % size;
    uptr initial_alloc = region_start;
    if (offset != 0)
      initial_alloc += (size - offset);

    region_next_alloc[i] = initial_alloc;
  }

  return true;
}

// Allocate from a FlexFat region
// First checks the free list, then falls back to bump allocation.
// Thread-safe: protected by per-size-class spin mutex.
//
// In right-align mode, returns the highest malloc-aligned address within the
// slot that still leaves room for the requested object. The bounds check
// (ptr - GetBase(ptr)) < class_size is still correct: GetBase() recovers
// slot_base via reciprocal multiplication since slot_base is always
// class-aligned, and any
// access past slot_base+class_size fails the check.
//
// The free list always stores slot bases (not right-aligned pointers) so that
// freed blocks can be reused with a different offset for a new request size.
static void *AllocateImpl(uptr size, uptr alignment) {
  if (size == 0)
    size = 1;

  uptr class_request = size;
  if (alignment) {
    if (alignment - 1 > ~(uptr)0 - class_request)
      return nullptr;
    class_request += alignment - 1;
  }

  uptr class_index = SizeClassIndex(class_request);
  if (class_index >= kNumSizeClasses)
    return nullptr;

  uptr alloc_size = SizeClassToSize(class_index);

  SpinMutexLock lock(&region_locks[class_index]);

  uptr slot_base;

  // 1. Try free list first (stores slot bases)
  FreeBlock *block = free_lists[class_index];
  if (block) {
    free_lists[class_index] = block->next;
    slot_base = (uptr)block;
    // Zero the entire slot (free list pointer was stored at slot_base)
    internal_memset(block, 0, alloc_size);
  } else {
    // 2. Fall back to bump allocation
    uptr region_end = GetRegionStart(class_index) + kRegionSize;
    uptr addr = region_next_alloc[class_index];

    if (addr + alloc_size > region_end) {
      // Interceptors perform matched system-allocation fallback.  Returning an
      // InternalAlloc pointer here would later send it to libc free().
      return nullptr;
    }

    region_next_alloc[class_index] = addr + alloc_size;
    slot_base = addr;
  }

  // In right-align mode, preserve malloc alignment by rounding the available
  // slack down to the nearest alignment boundary before shifting the pointer.
  uptr user = slot_base;
  if (alignment)
    user = RoundUpTo(slot_base, alignment);
  else if (flexfat_right_align)
    user += RoundDownTo(alloc_size - size, kMallocAlignment);
#ifdef FLEXFAT_TEMPORAL_TBI
  uptr base;
  auto *entry = TemporalEntry(slot_base, base);
  CHECK(entry);
  unsigned previous = atomic_load(entry, memory_order_relaxed) & 255;
  unsigned generation = previous == 255 ? 1 : previous + 1;
#ifdef FLEXFAT_EXACT_DEALLOCATION
  UserOffset(class_index, entry) = user - slot_base;
#endif
  atomic_store(entry, u16(generation | 256), memory_order_release);
  user = TagPointer(user, generation);
#endif
  return (void *)user;
}

void *Allocate(uptr size) { return AllocateImpl(size, 0); }

void *AllocateAligned(uptr size, uptr alignment) {
  CHECK(IsPowerOfTwo(alignment));
  return AllocateImpl(size, alignment);
}

// Free a FlexFat allocation by pushing its slot base onto the free list.
// Thread-safe: protected by per-size-class spin mutex.
//
// We always push the slot base (GetBase(ptr)) rather than ptr itself so that
// freed slots can be reused with a different right-align offset for a new
// request size, and so the free list is consistent regardless of mode.
void Deallocate(void *ptr) {
  if (!ptr)
    return;

  uptr addr = Untag((uptr)ptr);

  CHECK(IsFlexFatPointer(addr));

  uptr region = GetRegionIndex(addr);
  // Recover the slot base: in right-align mode ptr is offset within the slot;
  // in normal mode GetBase(addr) == addr since allocations are class-aligned.
  uptr slot_base = GetBase(addr);

#ifdef FLEXFAT_TEMPORAL_TBI
  TemporalState state;
  bool temporal_valid = false;
#ifdef FLEXFAT_EXACT_DEALLOCATION
  uptr expected = 0;
#endif
#endif
  bool valid = true;
  {
    SpinMutexLock lock(&region_locks[region]);
#ifdef FLEXFAT_TEMPORAL_TBI
    state = LoadTemporalState((uptr)ptr);
    temporal_valid = state.Matches((uptr)ptr);
    valid = temporal_valid;
#ifdef FLEXFAT_EXACT_DEALLOCATION
    if (temporal_valid)
      expected = state.base + UserOffset(region, state.entry);
    valid = temporal_valid && addr == expected;
#endif
    if (valid)
      atomic_store(state.entry, u16(state.bits & 255), memory_order_release);
#endif
    if (valid) {
      // Invalidate before overwriting the slot with the raw free-list link.
      FreeBlock *block = (FreeBlock *)slot_base;
      block->next = free_lists[region];
      free_lists[region] = block;
    }
  }
#ifdef FLEXFAT_TEMPORAL_TBI
  if (!temporal_valid)
    ReportTemporal((uptr)ptr, 0, 2, state);
#ifdef FLEXFAT_EXACT_DEALLOCATION
  if (!valid)
    ReportInvalidDeallocation((uptr)ptr, 2, expected, state.base);
#endif
#endif
}

static void PrintOobHeader(const char *level, uptr ptr, uptr base, uptr bound,
                           int is_write) {
  // Compute the signed overflow: how many bytes past the end of the allocation
  // the access reached. 0 means exactly at the boundary.
  sptr overflow = (sptr)(ptr) - (sptr)(base + bound);
  const char *op = is_write ? "write" : "read";

  Printf("FLEXFAT %s: out-of-bounds error detected!\n", level);
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

} // namespace __flexfat

// ---------------------- Interface Functions ----------------------

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_set_recover(int recover) {
  __flexfat::flexfat_recover = (recover != 0);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_set_right_align(int right_align) {
  __flexfat::flexfat_right_align = (right_align != 0);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_init() {
  using namespace __flexfat;
  u32 expected = unsigned(InitState::Uninitialized);
  if (!atomic_compare_exchange_strong(&init_state, &expected,
          unsigned(InitState::Initializing), memory_order_acq_rel))
    return;

  InitializeFlags();
#ifdef FLEXFAT_TEMPORAL_TBI
  EnableTaggedAddresses();
#endif

  __flexfat::InitTables();

  __flexfat::InitRegionTable();

  if (!__flexfat::InitMemoryRegions()) {
    Printf("FLEXFAT initialization failed: fixed spatial regions\n");
    Die();
  }
#ifdef FLEXFAT_TEMPORAL_TBI
  InitTemporal();
#endif
  __flexfat::InitializeInterceptors();
  atomic_store(&init_state, unsigned(InitState::Ready), memory_order_release);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_report_oob(uptr ptr, uptr base, uptr bound, int is_write) {
  GET_STACK_TRACE_FATAL_HERE;
  __flexfat::PrintErrorAndDie(ptr, base, bound, is_write, stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_warn_oob(uptr ptr, uptr base, uptr bound, int is_write) {
  GET_STACK_TRACE_FATAL_HERE;
  __flexfat::PrintWarning(ptr, base, bound, is_write, stack);
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_base(uptr ptr) {
  uptr base = __flexfat::GetBase(ptr);
#ifdef FLEXFAT_TEMPORAL_TBI
  if (__flexfat::IsFlexFatPointer(ptr))
    return __flexfat::TagPointer(base, __flexfat::PointerTag(ptr));
#endif
  return base;
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_size(uptr ptr) { return __flexfat::GetSize(ptr); }

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_offset(uptr ptr) {
  uptr base = __flexfat::GetBase(ptr);
  if (base == 0)
    return 0;
  return __flexfat::Untag(ptr) - base;
}

SANITIZER_INTERFACE_ATTRIBUTE
uptr __flexfat_get_usable_size(uptr ptr) {
  uptr base = __flexfat::GetBase(ptr);
  uptr size = __flexfat::GetSize(ptr);
  if (base == 0)
    return (uptr)-1;
  return size - (__flexfat::Untag(ptr) - base);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__flexfat_malloc(uptr size) { return __flexfat::Allocate(size); }

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_free(void *ptr) { __flexfat::Deallocate(ptr); }

#ifdef FLEXFAT_TEMPORAL_TBI
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_v1() {
  __flexfat_init();
  CHECK(__flexfat::IsReady());
}
SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_check_temporal(uptr ptr, uptr size, int operation) {
  __flexfat::ValidateTemporal(ptr, size, operation);
}
#endif

} // extern "C"

#if SANITIZER_CAN_USE_PREINIT_ARRAY
// ELF platforms: use .preinit_array for earliest possible initialization
__attribute__((section(".preinit_array"), used)) static auto preinit =
    __flexfat_init;
#else
// macOS/other platforms: use constructor attribute
__attribute__((constructor)) static void flexfat_constructor() {
  __flexfat_init();
}
#endif
