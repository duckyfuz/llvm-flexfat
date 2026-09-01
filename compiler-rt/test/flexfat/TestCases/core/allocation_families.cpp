// RUN: %clangxx_flexfat -fno-builtin-aligned_alloc -O0 %s -lstdc++ -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -fno-builtin-aligned_alloc -O2 %s -lstdc++ -o %t && %run %t 2>&1 | FileCheck %s

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <malloc.h>
#include <new>
#include <unistd.h>

using uptr = uintptr_t;
extern "C" uptr __flexfat_get_base(uptr);
extern "C" uptr __flexfat_get_size(uptr);

static bool managed(void *p) {
  return p && __flexfat_get_base((uptr)p) != 0 &&
         __flexfat_get_size((uptr)p) != (uptr)-1;
}

static bool aligned(void *p, uptr alignment) {
  return ((uptr)p & (alignment - 1)) == 0;
}

int main() {
  void *posix = nullptr;
  if (posix_memalign(&posix, 256, 33) || !managed(posix) ||
      !aligned(posix, 256))
    return 1;

  void *unchanged = (void *)0x1234;
  if (posix_memalign(&unchanged, 3, 32) != EINVAL ||
      unchanged != (void *)0x1234)
    return 2;

  void *aa = aligned_alloc(128, 256);
  void *ma = memalign(512, 19);
  void *va = valloc(27);
  void *pva = pvalloc(27);
  if (!managed(aa) || !aligned(aa, 128) || !managed(ma) || !aligned(ma, 512) ||
      !managed(va) || !aligned(va, (uptr)getpagesize()) || !managed(pva) ||
      !aligned(pva, (uptr)getpagesize()))
    return 3;

  errno = 0;
  if (aligned_alloc(64, 65) != nullptr || errno != EINVAL)
    return 4;

  char *dup = strdup("flexfat");
  char *ndup = strndup("abcdef", 3);
  if (!managed(dup) || !managed(ndup) || strcmp(dup, "flexfat") ||
      strcmp(ndup, "abc"))
    return 5;

  int *ordinary = new int(42);
  int *nothrow = new (std::nothrow) int(7);
  if (!managed(ordinary) || !managed(nothrow) || *ordinary != 42 ||
      *nothrow != 7)
    return 6;

  delete ordinary;
  delete nothrow;
  free(dup);
  free(ndup);
  free(pva);
  free(va);
  free(ma);
  free(aa);
  free(posix);

  // CHECK: allocation_families: ok
  write(1, "allocation_families: ok\n", 24);
  return 0;
}
