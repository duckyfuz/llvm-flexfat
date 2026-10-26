// REQUIRES: flexfat-tbi
// RUN: %clang -O2 %flexfat_config_flags -c %s -o %t.o
// RUN: %clangxx_flexfat %t.o -o %t && %run %t
// RUN: %clangxx_flexfat_tbi %t.o -o %t && %run %t
// RUN: %clangxx_flexfat -fsanitize-flexfat-tbi=prior-byte %t.o -o %t && %run %t

#include "flexfat/flexfat_config.h"
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

using namespace __flexfat;

extern "C" void __flexfat_init();
extern "C" uptr __flexfat_get_size(uptr);
extern "C" uptr __flexfat_get_base(uptr);

static void expect_unmapped(uptr address) {
  unsigned char resident;
  errno = 0;
  assert(mincore(reinterpret_cast<void *>(address), sysconf(_SC_PAGESIZE),
                 &resident) == -1);
  assert(errno == ENOMEM);
}

int main() {
  __flexfat_init();
  const uptr entries = kUserAddressLimit >> kRegionSizeLog;
  const uptr first = kRegionBase >> kRegionSizeLog;
  const uptr page = sysconf(_SC_PAGESIZE);
  const uptr bytes = (entries * sizeof(u64) + page - 1) & ~(page - 1);
#ifdef FLEXFAT_CUSTOM_CONFIG
  const uptr recovery_slot = 1;
#else
  const uptr recovery_slot = 3;
#endif
  auto *sizes = reinterpret_cast<volatile u64 *>(kTablesBase);
  auto *recovery = reinterpret_cast<volatile u64 *>(
      kTablesBase + recovery_slot * kTablesOffset);

  // Initialization must not commit foreign recovery pages just to write zeros.
  // This object is uninstrumented, so it has not read those entries either.
  unsigned char resident;
  assert(mincore(reinterpret_cast<void *>(
                     kTablesBase + recovery_slot * kTablesOffset + bytes - page),
                 page, &resident) == 0);
  assert(!(resident & 1));

  // Check sharing before exhaustive reads populate every alias's page table.
  const uptr private_begin = (first * sizeof(u64)) & ~(page - 1);
  const uptr private_end =
      ((first + kNumSizeClasses) * sizeof(u64) + page - 1) & ~(page - 1);
  uptr sentinel = bytes;
  for (uptr offset = 0; offset < bytes; offset += page)
    if (offset < private_begin || offset >= private_end) {
      sentinel = offset;
      break;
    }
  if (sentinel != bytes) {
    auto *source = reinterpret_cast<volatile u64 *>(kTablesBase + sentinel);
    assert(mprotect((void *)source, page, PROT_READ | PROT_WRITE) == 0);
    *source = 123;
    for (uptr offset = 0; offset < bytes; offset += page) {
      if (offset >= private_begin && offset < private_end)
        continue;
      assert(*reinterpret_cast<volatile u64 *>(kTablesBase + offset) == 123);
    }
    // Managed pages must retain their own backing, including foreign entries
    // in the boundary pages. Check the entire interval while aliases differ.
    for (uptr i = private_begin / sizeof(u64);
         i < private_end / sizeof(u64); ++i)
      assert(sizes[i] == (i >= first && i < first + kNumSizeClasses
                             ? SizeClassToSize(i - first) : ~u64(0)));
    *source = ~u64(0);
    assert(mprotect((void *)source, page, PROT_READ) == 0);
  }
  // Both private and shared portions must be protected after initialization.
  const uptr probes[] = {private_begin, sentinel == bytes ? private_begin : sentinel};
  for (uptr offset : probes) {
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
      *reinterpret_cast<volatile u64 *>(kTablesBase + offset) = 0;
      _exit(1);
    }
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
  }

  for (uptr i = 0; i < entries; ++i) {
    if (i < first || i >= first + kNumSizeClasses) {
      assert(sizes[i] == ~u64(0));
      assert(recovery[i] == 0);
      continue;
    }
    uptr size = SizeClassToSize(i - first);
    assert(sizes[i] == size);
#ifdef FLEXFAT_CUSTOM_CONFIG
    assert(recovery[i] == kFlexFatGenMagics[i - first]);
#else
    assert(recovery[i] == ~(u64(size) - 1));
#endif
  }
  assert(__flexfat_get_size(kUserAddressLimit - 1) == ~uptr(0));
  assert(__flexfat_get_base(kUserAddressLimit - 1) == 0);
  assert(__flexfat_get_size(kUserAddressLimit) == ~uptr(0));

  // No bias table is needed by spatial-only or in-slot temporal modes.
  for (uptr slot = 0; slot < 4; ++slot) {
    const bool used = slot == 0 || slot == recovery_slot;
    expect_unmapped(kTablesBase + slot * kTablesOffset + (used ? bytes : 0));
    expect_unmapped(kTablesBase + (slot + 1) * kTablesOffset - page);
  }
}
