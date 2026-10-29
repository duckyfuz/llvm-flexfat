//===-- flexfat_rtl.cpp - FlexFat Sanitizer Runtime Library
//---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
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
#include <pthread.h>
#if SANITIZER_LINUX
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_posix.h"
#include <sys/mman.h>
#endif
#include "sanitizer_common/sanitizer_atomic.h"
#ifdef FLEXFAT_TEMPORAL_TBI
#if !defined(__aarch64__) || !defined(__linux__) || __BYTE_ORDER__ != __ORDER_LITTLE_ENDIAN__ || __SIZEOF_POINTER__ != 8
#error FlexFat TBI requires little-endian Linux AArch64 with 64-bit pointers
#endif
#include "sanitizer_common/sanitizer_linux.h"
#include <linux/prctl.h>
#endif

using namespace __sanitizer;

#if defined(FLEXFAT_TBI_LAST_BYTE) || defined(FLEXFAT_TBI_PRIOR_BYTE)
extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE u8 __flexfat_tbi_zero_sentinel = 0;
}
#endif

namespace __flexfat {

// Initialization is published only after mappings and interceptors are ready.
static atomic_uint32_t init_state;
bool IsReady() {
  return atomic_load(&init_state, memory_order_acquire) == unsigned(InitState::Ready);
}

RegionInfo kRegions[kNumSizeClasses];

// LowFat allocator state: the first word of a free slot is reserved.
struct FreeBlock {
#if defined(FLEXFAT_TBI_LAST_BYTE) || defined(FLEXFAT_TBI_PRIOR_BYTE)
  // A second-word link would overwrite the generation in a 16-byte slot.
  FreeBlock *next;
  uptr reserved;
#else
  uptr reserved;
  FreeBlock *next;
#endif
};
struct RegionAllocator {
  pthread_mutex_t mutex;
  FreeBlock *freelist;
  uptr freeptr;
  uptr endptr;
  uptr accessptr;
};
static RegionAllocator region_allocators[kNumSizeClasses];

// Fixed table offsets are part of the compiler ABI, not mapping lengths.
// The pass uses absolute addressing for these tables:
//   slot 0: Sizes (8 bytes per address-region index)
//   slot 1: Reciprocal magics (custom mode)
//   slot 2: Reserved (unmapped)
//   slot 3: Masks (POW2 mode)
// Both build families obtain their layout from flexfat_layout.h.
static constexpr uptr kTableEntries = kUserAddressLimit >> kRegionSizeLog;
static constexpr uptr kManagedTableBegin = kRegionBase >> kRegionSizeLog;
static void MapMetadataTable(uptr slot, const char *name) {
  const uptr address = kTablesBase + slot * kTablesOffset;
  const uptr bytes = RoundUpTo(kTableEntries * sizeof(u64), GetPageSizeCached());
  CHECK_LE(bytes, kTablesOffset);
  if (!MemoryRangeIsAvailable(address, address + bytes - 1) ||
      !MmapFixedNoReserve(address, bytes, name)) {
    Printf("FLEXFAT initialization failed: fixed %s table\n", name);
    Die();
  }
}

#ifdef FLEXFAT_TEMPORAL_TBI
#if defined(FLEXFAT_CUSTOM_CONFIG) && !defined(FLEXFAT_TBI_LAST_BYTE) && \
    !defined(FLEXFAT_TBI_PRIOR_BYTE)
#error "Custom FlexFat TBI requires last-byte or prior-byte storage"
#endif

// All size-class layouts use zero-initialized generations modulo 256.
// There is no independent liveness tracking. A matching forged tag can pass
// for a freed slot, and stale tags can match again after generation wraparound.
// Generation bytes use ordinary accesses, like HWASan tags. Allocator
// updates are serialized by RegionLock; tag observations do not synchronize
// application accesses with a concurrent free.
#if !defined(FLEXFAT_TBI_LAST_BYTE) && !defined(FLEXFAT_TBI_PRIOR_BYTE)
// Both shadow layouts use this anchor as the foreign-pointer zero sentinel.
// Direct shadow uses slot_base >> 4; packed shadow uses a negative slot offset.
static constexpr uptr kTemporalShadowOffset = FLEXFAT_TEMPORAL_SHADOW_OFFSET;
#ifdef FLEXFAT_TBI_PACKED_SHADOW
// Classes occupy [anchor - 512 MiB, anchor); the separate final page is the
// foreign-pointer zero sentinel. Four bytes before the anchor are unused.
static constexpr uptr kTemporalShadowDataBytes = 1ULL << 29;
static constexpr uptr kTemporalShadowBytes = kTemporalShadowDataBytes + 4096;
static_assert(kRegionSizeLog == 32 && kMinSizeLog == 4 && kNumSizeClasses == 27,
              "Packed shadow requires the POW2 v1 geometry");
static_assert(kTemporalShadowOffset - kTemporalShadowDataBytes >=
                  kTablesBase + 4 * kTablesOffset,
              "Packed shadow overlaps spatial tables");
static_assert(kTemporalShadowOffset + 4096 <= kUserAddressLimit,
              "Packed shadow exceeds the user address range");
#else
static constexpr uptr kTemporalShadowBytes = FLEXFAT_DIRECT_SHADOW_BYTES;
#endif
static void InitTemporal() {
  // Reserve the selected shadow layout without committing unused pages.
  auto map_shadow = [](uptr address, uptr bytes) {
    if (!MemoryRangeIsAvailable(address, address + bytes - 1) ||
        !MmapFixedNoReserve(address, bytes, "flexfat_temporal_shadow")) {
      Printf("FLEXFAT initialization failed: fixed temporal shadow\n");
      Die();
    }
  };
#ifdef FLEXFAT_TBI_PACKED_SHADOW
  map_shadow(kTemporalShadowOffset - kTemporalShadowDataBytes,
             kTemporalShadowBytes);
#else
  map_shadow(kTemporalShadowOffset, kTemporalShadowBytes);
#endif
}
#endif

static u8 *ForeignTemporalEntry() {
#if defined(FLEXFAT_TBI_LAST_BYTE) || defined(FLEXFAT_TBI_PRIOR_BYTE)
  return &__flexfat_tbi_zero_sentinel;
#else
  return reinterpret_cast<u8 *>(kTemporalShadowOffset);
#endif
}

// Foreign addresses use the zero sentinel. Managed addresses must identify a
// complete slot; zero is a valid generation, not an invalid-geometry marker.
// Arithmetic stays unsigned until the final cast.
static u8 *TemporalEntry(uptr raw, uptr &base) {
  uptr region = GetRegionIndex(raw);
  if (region >= kNumSizeClasses)
    return ForeignTemporalEntry();
  uptr size = SizeClassToSize(region);
  uptr slot = raw / size;
  base = slot * size;
  uptr start = GetRegionStart(region), end = start + kRegionSize;
  if (base < start || base > end - size)
    return nullptr;
#ifdef FLEXFAT_TBI_PRIOR_BYTE
  return reinterpret_cast<u8 *>(base - 1);
#elif defined(FLEXFAT_TBI_LAST_BYTE)
  return reinterpret_cast<u8 *>(base + size - 1);
#elif defined(FLEXFAT_TBI_PACKED_SHADOW)
  const unsigned shift = unsigned(region + kMinSizeLog);
  const s64 biased = s64(raw & 0xffffffffULL) - s64(1ULL << 33);
  return reinterpret_cast<u8 *>(kTemporalShadowOffset + uptr(biased >> shift));
#else
  return reinterpret_cast<u8 *>(kTemporalShadowOffset + (base >> kMinSizeLog));
#endif
}

struct TemporalState {
  uptr base = 0;
  u8 *entry = nullptr;
  unsigned generation = 0;

  bool Matches(uptr ptr) const {
    return entry && generation == PointerTag(ptr);
  }
};

static TemporalState LoadTemporalState(uptr ptr) {
  TemporalState state;
  state.entry = TemporalEntry(Untag(ptr), state.base);
  if (state.entry)
    state.generation = *state.entry;
  return state;
}

// Reporting may allocate while symbolizing. Never hold an allocator lock here.
static void NORETURN ReportTemporal(uptr ptr, uptr access_size, int operation,
                                    const TemporalState &state, bool slot_valid) {
  const char *ops[] = {"read", "write", "free", "realloc"};
  const char *reason = !slot_valid ? "invalid slot geometry"
                       : "generation mismatch";
  Printf("FLEXFAT ERROR: temporal violation\n"
         "  operation = %s, tagged address = 0x%zx, raw slot base = 0x%zx\n"
         "  current generation = %u, pointer tag = %u, access size = %zu\n"
         "  metadata = %s, reason = %s\n",
         operation >= 0 && operation < 4 ? ops[operation] : "unknown",
         ptr, state.base, state.generation, PointerTag(ptr), access_size,
         slot_valid ? "available" : "unavailable (invalid slot geometry)",
         reason);
  GET_STACK_TRACE_FATAL_HERE;
  stack.Print();
  Die();
}

static void ValidateTemporal(uptr ptr, uptr access_size, int operation) {
  if (operation < 2 && !access_size)
    return;
  if (operation >= 2 && !IsFlexFatPointer(ptr))
    return;
  TemporalState state = LoadTemporalState(ptr);
  if (!state.Matches(ptr))
    ReportTemporal(ptr, access_size, operation, state, state.entry != nullptr);
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
  parser.ParseStringFromEnv("FLEXFAT_OPTIONS");

  InitializeCommonFlags();
  SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
}

#if SANITIZER_LINUX
static void NORETURN SizeTableMappingFailed(const char *operation, uptr address,
                                          uptr bytes, uptr result) {
  int error = 0;
  internal_iserror(result, &error);
  Printf("FLEXFAT initialization failed: size table %s "
         "(address=0x%zx, bytes=%zu, result=0x%zx, errno=%d)\n",
         operation, address, bytes, result, error);
  Die();
}

static void InitSizeTableBacking() {
  const uptr page = GetPageSizeCached();
  const uptr bytes = RoundUpTo(kTableEntries * sizeof(u64), page);
  const uptr managed_begin = RoundDownTo(kManagedTableBegin * sizeof(u64), page);
  const uptr managed_end =
      RoundUpTo((kManagedTableBegin + kNumSizeClasses) * sizeof(u64), page);
  // Pages intersecting managed entries retain their private backing, including
  // any foreign entries at the boundaries.
  internal_memset(reinterpret_cast<void *>(kTablesBase + managed_begin), 0xff,
                  managed_end - managed_begin);

  const uptr sentinel_offset = managed_begin == 0 ? managed_end : 0;
  if (sentinel_offset >= bytes)
    return;
  const uptr sentinel = kTablesBase + sentinel_offset;
  uptr result = internal_mmap(reinterpret_cast<void *>(sentinel), page,
                              PROT_READ | PROT_WRITE,
                              MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (result != sentinel)
    SizeTableMappingFailed("shared mapping", sentinel, page, result);
  internal_memset(reinterpret_cast<void *>(sentinel), 0xff, page);

  // All foreign-only pages alias one all-ones shared page. A zero-old-size
  // mremap clones that page without moving its source.
  for (uptr offset = 0; offset < bytes; offset += page) {
    if (offset == sentinel_offset ||
        (offset >= managed_begin && offset < managed_end))
      continue;
    const uptr address = kTablesBase + offset;
    const uptr alias = internal_mremap(reinterpret_cast<void *>(sentinel), 0,
                                      page, MREMAP_MAYMOVE | MREMAP_FIXED,
                                      reinterpret_cast<void *>(address));
    if (alias != address)
      SizeTableMappingFailed("alias mapping", address, page, alias);
  }
}
#endif

static void InitSpatialTables() {
  // Cover all user-address indices, leaving unused ABI slots and gaps unmapped.
  MapMetadataTable(0, "flexfat_sizes");
#ifdef FLEXFAT_CUSTOM_CONFIG
  MapMetadataTable(1, "flexfat_magics");
#else
  MapMetadataTable(3, "flexfat_masks");
#endif

  u64 *sizes = (u64 *)(kTablesBase + 0 * kTablesOffset);
#ifdef FLEXFAT_CUSTOM_CONFIG
  u64 *magics = (u64 *)(kTablesBase + 1 * kTablesOffset);
#else
  u64 *masks = (u64 *)(kTablesBase + 3 * kTablesOffset);
#endif

  // LowFat-style absolute indexing covers every user address below 2^48.
  // Foreign regions receive wide bounds and zero recovery metadata; managed
  // regions overwrite those sentinels with their real class metadata. Anonymous
  // mappings already provide zero magics/masks: do not fault foreign pages just
  // to write zeros. Only pages containing managed recovery entries need backing.
#if SANITIZER_LINUX
  InitSizeTableBacking();
#else
  for (uptr i = 0; i < kTableEntries; ++i)
    sizes[i] = ~(u64)0;
#endif

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
#if SANITIZER_LINUX
  const uptr bytes = RoundUpTo(kTableEntries * sizeof(u64), GetPageSizeCached());
  const uptr result = internal_mprotect(sizes, bytes, PROT_READ);
  if (result != 0)
    SizeTableMappingFailed("protection", kTablesBase, bytes, result);
#endif
}

// Ported LowFat allocation and page-management implementation.
#include "flexfat_lowfat_allocator.inc"

void Deallocate(void *ptr) {
  if (!ptr)
    return;

  uptr addr = Untag((uptr)ptr);

  CHECK(IsFlexFatPointer(addr));

  uptr region = GetRegionIndex(addr);
  // Aligned allocation can return an interior pointer, as in LowFat.
  // The free list always receives the recovered slot base.
  uptr slot_base = GetBase(addr);

#ifdef FLEXFAT_TEMPORAL_TBI
  TemporalState state;
#endif
  bool valid = true;
  {
    RegionLock lock(region_allocators[region]);
#ifdef FLEXFAT_TEMPORAL_TBI
#if defined(FLEXFAT_TBI_LAST_BYTE) || defined(FLEXFAT_TBI_PRIOR_BYTE)
    state.base = slot_base;
    uptr slot_size = SizeClassToSize(region);
    uptr region_start = GetRegionStart(region);
    uptr min_base = region_start;
#ifdef FLEXFAT_TBI_PRIOR_BYTE
    min_base += slot_size;
#endif
    if (slot_base >= min_base &&
        slot_base <= region_start + kRegionSize - slot_size) {
#ifdef FLEXFAT_TBI_PRIOR_BYTE
      state.entry = reinterpret_cast<u8 *>(slot_base - 1);
#else
      state.entry = reinterpret_cast<u8 *>(slot_base + slot_size - 1);
#endif
      state.generation = *state.entry;
    }
#else
    state = LoadTemporalState((uptr)ptr);
#endif
    valid = state.Matches((uptr)ptr);
#endif
    if (valid) {
#ifdef FLEXFAT_TEMPORAL_TBI
      *state.entry = u8(state.generation + 1);
#endif
      // Invalidate before overwriting the slot with the raw free-list link.
      FreeBlock *block = (FreeBlock *)slot_base;
      ReleaseSlotPages(slot_base, SizeClassToSize(region));
      block->next = region_allocators[region].freelist;
      region_allocators[region].freelist = block;
    }
  }
#ifdef FLEXFAT_TEMPORAL_TBI
  if (!valid)
    ReportTemporal((uptr)ptr, 0, 2, state, state.entry != nullptr);
#endif
}

} // namespace __flexfat

extern "C" {

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

  __flexfat::InitSpatialTables();

  __flexfat::InitRegionTable();

  if (!__flexfat::InitMemoryRegions()) {
    Printf("FLEXFAT initialization failed: fixed spatial regions\n");
    Die();
  }
#ifdef FLEXFAT_TEMPORAL_TBI
#if !defined(FLEXFAT_TBI_LAST_BYTE) && !defined(FLEXFAT_TBI_PRIOR_BYTE)
  InitTemporal();
#endif
#endif
  __flexfat::InitializeInterceptors();
  atomic_store(&init_state, unsigned(InitState::Ready), memory_order_release);
}

SANITIZER_INTERFACE_ATTRIBUTE NORETURN
void __flexfat_report_oob(uptr ptr, uptr base, uptr bound, int is_write) {
  sptr overflow = (sptr)ptr - (sptr)(base + bound);

  Printf("FLEXFAT ERROR: out-of-bounds error detected!\n");
  Printf("          operation = %s\n", is_write ? "write" : "read");
  Printf("          pointer   = 0x%zx (heap)\n", ptr);
  Printf("          base      = 0x%zx\n", base);
  Printf("          size      = %zu\n", bound);
  Printf("          overflow  = %s%zd\n\n", overflow >= 0 ? "+" : "",
         (long)overflow);
  GET_STACK_TRACE_FATAL_HERE;
  stack.Print();
  Die();
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
  return __flexfat::UsableClassSize(size) - (__flexfat::Untag(ptr) - base);
}

SANITIZER_INTERFACE_ATTRIBUTE
void *__flexfat_malloc(uptr size) { return __flexfat::Allocate(size); }

SANITIZER_INTERFACE_ATTRIBUTE
void __flexfat_free(void *ptr) { __flexfat::Deallocate(ptr); }

#ifdef FLEXFAT_TEMPORAL_TBI
#ifdef FLEXFAT_TBI_PRIOR_BYTE
#ifdef FLEXFAT_CUSTOM_CONFIG
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_prior_byte_custom_v3() {
#else
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_prior_byte_pow2_v3() {
#endif
#elif defined(FLEXFAT_TBI_LAST_BYTE)
#ifdef FLEXFAT_CUSTOM_CONFIG
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_last_byte_custom_v2() {
#else
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_last_byte_pow2_v2() {
#endif
#elif defined(FLEXFAT_TBI_PACKED_SHADOW)
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_packed_shadow_pow2_v1() {
#else
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_v7() {
#endif
  __flexfat_init();
  CHECK(__flexfat::IsReady());
}
SANITIZER_INTERFACE_ATTRIBUTE NORETURN __attribute__((cold)) void
__flexfat_report_temporal_v3(uptr ptr, uptr size, u32 operation,
                             u32 observed_generation) {
  // Reconstruct only diagnostic geometry; never reread generation state.
  __flexfat::TemporalState state;
  state.entry = __flexfat::TemporalEntry(__flexfat::Untag(ptr), state.base);
  state.generation = observed_generation;
  __flexfat::ReportTemporal(ptr, size, operation, state,
                            state.entry != nullptr);
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
