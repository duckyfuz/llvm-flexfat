// REQUIRES: flexfat-tbi
// RUN: %clang -fno-builtin -c %s -o %t.o
// RUN: %clangxx_flexfat %t.o -Wl,--wrap=dlsym -Wl,--wrap=_ZN11__sanitizer18internal_allocatorEv -o %t && %run %t
// RUN: %clangxx_flexfat_tbi %t.o -Wl,--wrap=dlsym -Wl,--wrap=_ZN11__sanitizer18internal_allocatorEv -o %t && %run %t

#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern void *__real_dlsym(void *, const char *);
static unsigned bootstrap_allocations;
static const size_t requests[] = {37, 37, 37, 37, 256, 37, 37,
                                  2 * 1024 * 1024 + 123};
enum { allocation_kinds = sizeof(requests) / sizeof(requests[0]) };
static char *saved[128][allocation_kinds];
static size_t bootstrap_sizes[128][allocation_kinds];

// Catch even ownership queries: those used to initialize the internal allocator
// on ordinary free/realloc/usable-size calls, without an InternalAlloc call.
void *__real__ZN11__sanitizer18internal_allocatorEv(void);
void *__wrap__ZN11__sanitizer18internal_allocatorEv(void) {
#if defined(__GLIBC__)
  abort();
#else
  return __real__ZN11__sanitizer18internal_allocatorEv();
#endif
}

void *__wrap_dlsym(void *handle, const char *name) {
  unsigned n = bootstrap_allocations++;
  assert(n < 128);
  saved[n][0] = malloc(37);
  // This runs before even the first real interceptor has been resolved.
  assert(malloc_usable_size(saved[n][0]) >= 37);
  assert(malloc_usable_size(NULL) == 0);
  saved[n][1] = calloc(37, 1);
  assert(posix_memalign((void **)&saved[n][2], 256, 37) == 0);
  saved[n][3] = memalign(256, 37);
  saved[n][4] = aligned_alloc(256, 256);
  saved[n][5] = valloc(37);
  saved[n][6] = pvalloc(37);
  // Exercise libc's mmap allocation layout as well as arena chunks.
  saved[n][7] = malloc(requests[7]);
  for (unsigned j = 0; j < allocation_kinds; ++j) {
    assert(saved[n][j] && !((uintptr_t)saved[n][j] >> 56));
    bootstrap_sizes[n][j] = malloc_usable_size(saved[n][j]);
    assert(bootstrap_sizes[n][j] >= requests[j]);
    if (j == 1)
      for (unsigned k = 0; k < 37; ++k)
        assert(saved[n][j][k] == 0);
    memset(saved[n][j], 7 + j, 37);
  }
  for (unsigned j = 2; j <= 4; ++j)
    assert(!((uintptr_t)saved[n][j] & 255));
  assert((uintptr_t)saved[n][5] % sysconf(_SC_PAGESIZE) == 0);
  assert((uintptr_t)saved[n][6] % sysconf(_SC_PAGESIZE) == 0);
  // Exercise startup realloc/free as well as objects retained until readiness.
  char *p = realloc(NULL, 19);
  assert(p);
  assert(malloc_usable_size(p) >= 19);
  memset(p, 42, 19);
  p = realloc(p, 71);
  assert(p && p[18] == 42);
  assert(malloc_usable_size(p) >= 71);
  free(p);
  return __real_dlsym(handle, name);
}

int main(void) {
  assert(bootstrap_allocations > 0);
  for (unsigned n = 0; n < bootstrap_allocations; ++n)
    for (unsigned j = 0; j < allocation_kinds; ++j) {
      char *p = saved[n][j];
      // Compare the bootstrap answer against the resolved libc implementation.
      assert(malloc_usable_size(p) == bootstrap_sizes[n][j]);
      for (unsigned k = 0; k < 37; ++k)
        assert(p[k] == 7 + j);
#if defined(__GLIBC__)
      char *zero = realloc(p, 0);
      assert(zero && p[36] == 7 + j);
      free(zero);
#endif
      p = realloc(p, 4096);
      assert(p && malloc_usable_size(p) >= 4096);
      for (unsigned k = 0; k < 37; ++k)
        assert(p[k] == 7 + j);
      free(p);
    }
  char *p = malloc(17);
  assert(p && malloc_usable_size(p) >= 17);
  p = realloc(p, 128);
  assert(p);
  free(p);
  free(NULL);
  assert(malloc_usable_size(NULL) == 0);
  void *unchanged = (void *)123;
  errno = EDOM;
  assert(posix_memalign(&unchanged, 256, SIZE_MAX) == ENOMEM);
  assert(unchanged == (void *)123 && errno == EDOM);
}
