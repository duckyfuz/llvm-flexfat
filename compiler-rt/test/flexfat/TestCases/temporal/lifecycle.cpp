// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -fno-builtin -O0 %s -pthread -o %t && %t
// RUN: %clangxx_flexfat_tbi -fno-builtin -O2 %s -pthread -o %t && %t
// RUN: not %t read 2>&1 | FileCheck %s --check-prefix=READ
// RUN: not %t reuse 2>&1 | FileCheck %s --check-prefix=READ
// RUN: not %t write 2>&1 | FileCheck %s --check-prefix=WRITE
// RUN: not %t double-free 2>&1 | FileCheck %s --check-prefix=FREE
// RUN: not %t realloc 2>&1 | FileCheck %s --check-prefix=REALLOC
// RUN: not %t dead-interior-free 2>&1 | FileCheck %s --check-prefixes=TEMPORAL,FREE
// RUN: not %t stale-interior-realloc 2>&1 | FileCheck %s --check-prefixes=TEMPORAL,REALLOC
// TEMPORAL: FLEXFAT ERROR: temporal violation
// READ: operation = read
// WRITE: operation = write
// FREE: operation = free
// REALLOC: operation = realloc
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <sys/resource.h>
extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);
extern "C" uintptr_t __flexfat_get_offset(uintptr_t);
extern "C" uintptr_t __flexfat_get_usable_size(uintptr_t);
extern "C" void __flexfat_check_temporal(uintptr_t, uintptr_t, int);
static uintptr_t raw(void *p) { return (uintptr_t)p & 0x00ffffffffffffffULL; }
static unsigned tag(void *p) { return (uintptr_t)p >> 56; }
__attribute__((noinline)) static char *opaque(char *p) {
  asm volatile("" : "+r"(p) : : "memory"); return p;
}
__attribute__((noinline)) static int read_byte(char *p) { return *(volatile char *)p; }
__attribute__((noinline)) static void write_byte(char *p) { *(volatile char *)p = 9; }
static char *before_main;
__attribute__((constructor)) static void startup() {
  before_main = (char *)malloc(31); write_byte(before_main);
}
static void *release(void *p) { free(p); return nullptr; }
int main(int argc, char **argv) {
  assert(tag(before_main)); assert(read_byte(before_main) == 9); free(before_main);
  char *p = opaque((char *)malloc(23)); assert(p && tag(p));
  write_byte(p); assert(read_byte(p) == 9);
  uintptr_t base = __flexfat_get_base((uintptr_t)(p + 2));
  assert((base >> 56) == tag(p));
  assert(__flexfat_get_offset((uintptr_t)p) == raw(p) - (base & 0x00ffffffffffffffULL));
  assert(__flexfat_get_usable_size((uintptr_t)p) <= __flexfat_get_size((uintptr_t)p));
  if (argc > 1) {
    const char *mode = argv[1];
    if (!strcmp(mode, "foreign")) {
      // Bypass the executable's interceptor to obtain a real libc allocation.
      auto system_malloc = (void *(*)(size_t))dlsym(RTLD_NEXT, "malloc");
      assert(system_malloc);
      char *q = (char *)system_malloc(32); assert(q && !tag(q));
      q[0] = 17;
      q = (char *)realloc(q, 64); assert(q && !tag(q) && q[0] == 17);
      free(q);
      q = (char *)system_malloc(32); assert(q && !tag(q));
      assert(!realloc(q, 0));
      free(p); return 0;
    }
    if (!strcmp(mode, "zero-tag")) return read_byte((char *)raw(p));
    if (!strcmp(mode, "geometry") || !strcmp(mode, "geometry-tail")) {
      for (unsigned n = 33; n < 1000; n += 16) {
        char *q = (char *)malloc(n);
        uintptr_t region = raw(q) & ~((1ULL << 38) - 1);
        uintptr_t size = __flexfat_get_size((uintptr_t)q);
        if (!strcmp(mode, "geometry") && region % size)
          __flexfat_check_temporal((1ULL << 56) | region, 1, 0);
        uintptr_t end = region + (1ULL << 38);
        if (!strcmp(mode, "geometry-tail") && end % size)
          __flexfat_check_temporal((1ULL << 56) | (end - 1), 1, 0);
        free(q);
      }
      abort(); // Custom configuration must have a region with prefix padding.
    }
    if (!strcmp(mode, "never")) {
      uintptr_t next = (base & 0x00ffffffffffffffULL) + 100 * __flexfat_get_size((uintptr_t)p);
      __flexfat_check_temporal((1ULL << 56) | next, 1, 0); return 0;
    }
    if (!strcmp(mode, "realloc-failure")) {
      // SIZE_MAX cannot be satisfied by libc, independently of overcommit.
      volatile size_t impossible = SIZE_MAX;
      assert(!realloc(p, impossible)); assert(read_byte(p) == 9); free(p); return 0;
    }
    if (!strcmp(mode, "fallback")) {
      // Force the managed-to-system path without committing the huge allocation.
      size_t large = 128ULL << 30;
      char *q = (char *)realloc(p, large);
      if (q) { assert(!tag(q)); assert(read_byte(q) == 9); free(q); }
      else { assert(read_byte(p) == 9); free(p); }
      return 0;
    }
    if (!strcmp(mode, "thread")) {
      pthread_t t; assert(!pthread_create(&t, nullptr, release, p));
      assert(!pthread_join(t, nullptr)); return read_byte(p);
    }
    free(p);
    if (!strcmp(mode, "reuse") || !strcmp(mode, "stale-free") || !strcmp(mode, "stale-realloc") ||
        !strcmp(mode, "stale-interior-free") || !strcmp(mode, "stale-interior-realloc")) {
      char *q = opaque((char *)malloc(23)); assert(raw(q) == raw(p)); assert(tag(q) != tag(p));
    }
    if (!strcmp(mode, "write")) write_byte(p);
    else if (!strcmp(mode, "dead-interior-free") || !strcmp(mode, "stale-interior-free")) free(opaque(p + 1));
    else if (!strcmp(mode, "dead-interior-realloc") || !strcmp(mode, "stale-interior-realloc")) {
      p = (char *)realloc(opaque(p + 1), 24); asm volatile("" : : "r"(p) : "memory");
    }
    else if (!strcmp(mode, "double-free") || !strcmp(mode, "stale-free")) free(p);
    else if (!strcmp(mode, "realloc") || !strcmp(mode, "stale-realloc")) {
      p = (char *)realloc(p, 24); asm volatile("" : : "r"(p) : "memory");
    } else if (!strcmp(mode, "realloc-zero")) {
      p = (char *)realloc(p, 0); asm volatile("" : : "r"(p) : "memory");
    } else if (!strcmp(mode, "memset")) memset(p, 0, 1);
    else if (!strcmp(mode, "memcpy-src")) { char out[8]; memcpy(out, p, 1); asm volatile("" : : "r"(out) : "memory"); }
    else if (!strcmp(mode, "memcpy-dst")) memcpy(p, "x", 1);
    else if (!strcmp(mode, "memmove")) memmove(p, "x", 1);
    else if (!strcmp(mode, "strdup")) { char *q = strdup(p); asm volatile("" : : "r"(q) : "memory"); }
    else if (!strcmp(mode, "strndup")) { char *q = strndup(p, 1); asm volatile("" : : "r"(q) : "memory"); }
    else if (!strcmp(mode, "posix")) return posix_memalign((void **)p, 64, 16);
    else if (!strcmp(mode, "zero-length")) {
      memset(p, 0, 0); memcpy(p, p, 0); memmove(p, p, 0);
      char *q = strndup(p, 0); assert(q && !q[0]); free(q); return 0;
    } else return read_byte(p);
    return 0;
  }
  assert(memset(p, 0, 3) == p);
  assert(memcpy(p, "abc", 3) == p);
  assert(memmove(p + 1, p, 2) == p + 1);
  write_byte(p);
  char *dup = strdup("abc"); assert(tag(dup) && !strcmp(dup, "abc")); free(dup);
  dup = strndup("abc", 2); assert(tag(dup) && !strcmp(dup, "ab")); free(dup);
  // Both layouts have enough slack here to shift the user pointer in
  // right-align mode. Realloc must copy from that interior user address.
  char *shifted = (char *)malloc(273);
  shifted[0] = 17; shifted[256] = 29;
  char *grown = (char *)realloc(shifted, 513);
  assert(grown && grown[0] == 17 && grown[256] == 29);
  uintptr_t grown_base = __flexfat_get_base((uintptr_t)grown);
  uintptr_t grown_offset = __flexfat_get_offset((uintptr_t)grown);
  assert(read_byte((char *)(grown_base + grown_offset)) == 17);
  free(grown);
  char *q = (char *)realloc(p, 100); assert(q && read_byte(q) == 9); free(q);
  p = (char *)malloc(23); assert(!realloc(p, 0));
  p = (char *)calloc(16, 1); assert(p && tag(p));
  for (unsigned i = 0; i < 16; ++i) assert(p[i] == 0);
  free(p);
  void *aligned = nullptr; assert(!posix_memalign(&aligned, 256, 50));
  assert(!(raw(aligned) % 256)); assert(tag(aligned));
  write_byte((char *)aligned); free(aligned);
  p = (char *)malloc(23); unsigned first = tag(p); uintptr_t slot = raw(p);
  for (unsigned i = 0; i < 255; ++i) {
    free(p); p = opaque((char *)malloc(23)); assert(raw(p) == slot);
    assert(tag(p) == (first + i) % 255 + 1); write_byte(p);
  }
  assert(tag(p) == first);
  int fd = open("/dev/null", O_WRONLY); assert(fd >= 0);
  assert(write(fd, p, 1) == 1); close(fd); free(p);
  int local = 0; __flexfat_check_temporal((uintptr_t)&local, sizeof(local), 0);
  assert(__flexfat_get_base((uintptr_t)&local) == 0);
  assert(__flexfat_get_size((uintptr_t)&local) == UINTPTR_MAX);
  return 0;
}
