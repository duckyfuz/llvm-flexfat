// RUN: %clangxx_flexfat -fno-builtin -O0 %s -pthread -o %t && %run %t
// RUN: %clangxx_flexfat -fno-builtin -O2 %s -pthread -o %t && %run %t

#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);

static uintptr_t raw(const void *p) {
  return (uintptr_t)p & 0x00ffffffffffffffULL;
}

// Bypass spatial/temporal instrumentation to test actual VM permissions and
// payload retention, rather than the compiler's bounds checks.
__attribute__((noinline,
               disable_sanitizer_instrumentation)) static unsigned char
load(uintptr_t address) {
  return *(volatile unsigned char *)address;
}

static void expect_protected(uintptr_t address) {
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    struct rlimit limit = {0, 0};
    setrlimit(RLIMIT_CORE, &limit);
    _exit(load(address));
  }
  int status;
  assert(waitpid(child, &status, 0) == child);
  assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV);
}

static void *exercise(void *) {
  for (unsigned i = 0; i < 500; ++i) {
    char *p = (char *)malloc(23);
    assert(p);
    p[0] = 42;
    char *q = (char *)realloc(p, 24);
    assert(q == p && q[0] == 42);
    free(q);
  }
  return nullptr;
}

int main() {
  // LowFat leaves the first word and payload in freed small objects intact.
  char *p = (char *)malloc(23);
  assert(p && raw(p) == raw((void *)__flexfat_get_base((uintptr_t)p)));
  p[0] = 31;
  p[20] = 73;
  uintptr_t saved = raw(p);
  free(p);
  p = (char *)malloc(23);
  assert(raw(p) == saved);
  assert(load(raw(p) + 20) == 73);
#ifndef TEST_TBI_IN_SLOT
  assert(load(raw(p)) == 31);
#endif
  // Same-class realloc preserves the pointer, payload, and TBI generation.
  char *q = (char *)realloc(p, 24);
  assert(q == p && q[20] == 73);
  q = (char *)realloc(p, 0);
  assert(q && raw(q) != raw(p) && p[20] == 73);
  free(q);
  free(p);

  size_t page = (size_t)sysconf(_SC_PAGESIZE);
  // Pick a large slot, then recycle it for a request just over the previous
  // class boundary. The slot has whole-page slack after the requested data.
  size_t request = 256 * page + 1;
  p = (char *)malloc(request);
  size_t slot = __flexfat_get_size((uintptr_t)p);
  assert(slot > request + 2 * page);
  size_t middle = 4 * page;
  p[middle] = 11;
  unsigned old_tag = (uintptr_t)p >> 56;
  uintptr_t base = raw(p);
  uintptr_t reclaimed = (base + middle) & ~(page - 1);
  unsigned char resident;
  assert(mincore((void *)reclaimed, page, &resident) == 0 && (resident & 1));
  free(p);
  assert(mincore((void *)reclaimed, page, &resident) == 0 && !(resident & 1));
  expect_protected(reclaimed);
  p = (char *)malloc(request);
  assert(raw(p) == base && p[middle] == 0);
#ifdef TEST_TBI
  assert(((uintptr_t)p >> 56) != old_tag);
#endif
  // Reused large slots retain inaccessible guard pages after the payload.
  uintptr_t guard = (base + request + page - 1) & ~(page - 1);
  expect_protected(guard);
  q = (char *)realloc(p, request + 1);
  assert(q == p);
  // LowFat's same-class realloc opens the full slot, including guard pages.
  assert(load(guard) == 0);
  free(q);

  // Exercise minimum slots: in-slot TBI must not overwrite the generation
  // with the second word of LowFat's free-list node.
  p = (char *)malloc(15);
  q = (char *)malloc(15);
  base = raw(p);
  old_tag = (uintptr_t)p >> 56;
  free(p);
  p = (char *)malloc(15);
  assert(raw(p) == base);
#ifdef TEST_TBI
  assert(((uintptr_t)p >> 56) != old_tag);
#endif
  *q = 9;
  assert(*q == 9);
  free(q);
  free(p);

  // LowFat's size < alignment fast path selects an alignment-sized slot,
  // including the largest POW2 class. It must not add alignment padding again.
  void *aligned = nullptr;
  size_t alignment = 1ULL << 30;
  assert(posix_memalign(&aligned, alignment, 15) == 0);
  assert(aligned && raw(aligned) % alignment == 0);
  assert(raw(aligned) == raw((void *)__flexfat_get_base((uintptr_t)aligned)));
  assert(__flexfat_get_size((uintptr_t)aligned) == alignment);
  *(char *)aligned = 7;
  free(aligned);

  pthread_t threads[4];
  for (auto &thread : threads)
    assert(pthread_create(&thread, nullptr, exercise, nullptr) == 0);
  for (auto &thread : threads)
    assert(pthread_join(thread, nullptr) == 0);
}
