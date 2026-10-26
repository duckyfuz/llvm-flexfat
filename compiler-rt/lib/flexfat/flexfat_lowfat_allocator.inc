// LowFat allocator port, adapted to FlexFat's region and interceptor ABI.
// Derived from lowfat_malloc.c, Gregory J. Duck.
// Copyright (c) 2017 The National University of Singapore.
// All rights reserved. University of Illinois/NCSA Open Source License;
// see LICENSE.lowfat for the license, conditions and disclaimer.
//
// Included inside namespace __flexfat, after the temporal metadata helpers.

class RegionLock {
  RegionAllocator &info;

public:
  explicit RegionLock(RegionAllocator &info) : info(info) {
    CHECK_EQ(pthread_mutex_lock(&info.mutex), 0);
  }
  ~RegionLock() { CHECK_EQ(pthread_mutex_unlock(&info.mutex), 0); }
};

static uptr PagesBase(uptr ptr) {
  return RoundDownTo(ptr, GetPageSizeCached());
}

static uptr PagesSize(uptr ptr, uptr size) {
  return RoundUpTo(ptr - PagesBase(ptr) + size, GetPageSizeCached());
}

static uptr BigObjectSize() { return 3 * GetPageSizeCached(); }

// LowFat's generated heap selector requires size < class size. FlexFat's
// corrected reciprocal base recovery needs no additional precision padding.
uptr AllocationClass(uptr size) {
  if (size == ~(uptr)0)
    return kNumSizeClasses;
  return SizeClassIndex(size + 1);
}

static void InitRegionTable() {
  for (uptr i = 0; i < kNumSizeClasses; ++i) {
    kRegions[i].size = SizeClassToSize(i);
    kRegions[i].alignment = kRegions[i].size;
  }
}

static bool InitMemoryRegions() {
#ifdef FLEXFAT_TBI_PRIOR_BYTE
  const uptr page = GetPageSizeCached();
  if (reinterpret_cast<uptr>(MmapFixedNoAccess(kRegionBase - page, page,
                                               "flexfat_prior_byte_guard")) !=
          kRegionBase - page ||
      !MprotectReadOnly(kRegionBase - page, page))
    return false;
#endif
  for (uptr i = 0; i < kNumSizeClasses; ++i) {
    uptr start = GetRegionStart(i);
    if (reinterpret_cast<uptr>(
            MmapFixedNoAccess(start, kRegionSize, "flexfat_region")) != start)
      return false;
#ifdef FLEXFAT_TBI_PRIOR_BYTE
    // Instrumentation can read base - 1 at a region boundary, even before
    // that region has allocated a slot. Keep the zero sentinel addressable.
    if (!MprotectReadWrite(start + kRegionSize - page, page))
      return false;
#endif

    u32 offset;
    if (!GetRandom(&offset, sizeof(offset)))
      return false;
    // LowFat uses a 32-bit random offset in its larger heap regions. Limit
    // that offset for FlexFat's 4 GiB POW2 regions so all classes still fit.
    uptr mask = Min<uptr>(0xffffffffULL, kRegionSize / 8 - 1);
    uptr size = SizeClassToSize(i);
    uptr first = start + (offset & mask) + size + GetPageSizeCached();
    first -= first % size;
    RegionAllocator &info = region_allocators[i];
    if (pthread_mutex_init(&info.mutex, nullptr))
      return false;
    info.freelist = nullptr;
    info.freeptr = first;
    info.endptr = start + kRegionSize;
    info.accessptr = PagesBase(first);
  }
  return true;
}

static void MakeTagAccessible(uptr base, uptr size, uptr access_end,
                              bool reused) {
#if defined(FLEXFAT_TBI_LAST_BYTE) || defined(FLEXFAT_TBI_PRIOR_BYTE)
  // Free never reclaims the metadata page. Fresh payload mappings normally
  // already cover the tag too; avoid an mprotect syscall on small allocations.
  if (reused)
    return;
#ifdef FLEXFAT_TBI_PRIOR_BYTE
  uptr tag = base - 1;
#else
  uptr tag = base + size - 1;
#endif
  uptr page = PagesBase(tag);
  if (page < PagesBase(base) || page >= access_end)
    CHECK(MprotectReadWrite(page, GetPageSizeCached()));
#endif
}

// Like lowfat_free(), keep the first page for the intrusive free-list node
// and return the complete remaining pages to the OS. In-slot TBI generations
// must survive both MADV_DONTNEED and PROT_NONE, including the next slot's
// generation in prior-byte mode.
static void ReleaseSlotPages(uptr base, uptr size) {
  if (size < BigObjectSize())
    return;
  uptr begin = PagesBase(base) + GetPageSizeCached();
  uptr end = PagesBase(base + size);
#if defined(FLEXFAT_TBI_LAST_BYTE) || defined(FLEXFAT_TBI_PRIOR_BYTE)
  end = Min(end, PagesBase(base + size - 1));
#endif
  if (begin < end) {
    ReleaseMemoryPagesToOS(begin, end);
    CHECK(MprotectNoAccess(begin, end - begin));
  }
}

void MakeAllocationAccessible(uptr ptr) {
  uptr base = GetBase(ptr);
  uptr size = GetSize(ptr);
  if (size >= BigObjectSize())
    CHECK(MprotectReadWrite(PagesBase(base), PagesSize(base, size)));
}

// This is lowfat_malloc_index(): free-list first, then fresh space. No payload
// clearing or right-biased placement. The interceptor owns libc fallback.
void *Allocate(uptr size) {
  uptr index = AllocationClass(size);
  if (index >= kNumSizeClasses)
    return nullptr;
  uptr alloc_size = SizeClassToSize(index);
  RegionAllocator &info = region_allocators[index];
  uptr base;
  {
    RegionLock lock(info);
    bool reused = info.freelist != nullptr;
    if (FreeBlock *block = info.freelist) {
      info.freelist = block->next;
      base = reinterpret_cast<uptr>(block);
      if (alloc_size >= BigObjectSize()) {
        uptr begin = PagesBase(base) + GetPageSizeCached();
        uptr end = PagesBase(base) + PagesSize(base, size);
        if (begin < end)
          CHECK(MprotectReadWrite(begin, end - begin));
      }
    } else {
      base = info.freeptr;
      if (base > info.endptr - alloc_size)
        return nullptr;
      info.freeptr = base + alloc_size;
      if (info.freeptr > info.accessptr) {
        uptr begin = PagesBase(base);
        uptr bytes = Max(PagesSize(base, size), BigObjectSize());
        bytes = Min(bytes, info.endptr - begin);
        CHECK(MprotectReadWrite(begin, bytes));
        info.accessptr = begin + bytes;
      }
    }
    MakeTagAccessible(base, alloc_size, info.accessptr, reused);
#ifdef FLEXFAT_TEMPORAL_TBI
    uptr recovered_base;
    auto *entry = TemporalEntry(base, recovered_base);
    CHECK(entry);
    unsigned generation = *entry;
#ifdef FLEXFAT_CUSTOM_CONFIG
    if (!generation) {
      generation = 1;
      *entry = u8(generation);
    }
#endif
    return reinterpret_cast<void *>(TagPointer(base, generation));
#else
    return reinterpret_cast<void *>(base);
#endif
  }
}

// lowfat_posix_memalign(): use the minimum-alignment allocation directly,
// choose an alignment-sized slot for small objects, otherwise overallocate
// and return an aligned interior pointer. Preserve a TBI tag while rounding.
void *AllocateAligned(uptr size, uptr alignment) {
  CHECK(IsPowerOfTwo(alignment));
  if (alignment <= kMinSize)
    return Allocate(size);
  if (size < alignment)
    return Allocate(alignment - 1);
  if (size > ~(uptr)0 - (alignment - 1))
    return nullptr;
  void *ptr = Allocate(size + alignment - 1);
  if (!ptr)
    return nullptr;
  uptr raw = Untag(reinterpret_cast<uptr>(ptr));
  uptr aligned = RoundUpTo(raw, alignment);
  return reinterpret_cast<void *>(TagPointer(aligned, PointerTag((uptr)ptr)));
}
