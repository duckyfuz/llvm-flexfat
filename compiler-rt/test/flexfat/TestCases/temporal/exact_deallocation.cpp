// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -fsanitize-flexfat-deallocation-check=exact -fno-builtin -O2 %s -pthread -o %t
// RUN: %t regular valid
// RUN: %t aligned valid
// RUN: not %t regular after free 2>&1 | FileCheck %s --check-prefix=FREE
// RUN: not %t regular after realloc 2>&1 | FileCheck %s --check-prefix=REALLOC
// RUN: not %t regular after realloc-zero 2>&1 | FileCheck %s --check-prefix=REALLOC
// RUN: not %t aligned after free 2>&1 | FileCheck %s --check-prefix=FREE
// RUN: %clangxx_flexfat_tbi -fsanitize-flexfat-deallocation-check=exact -fno-builtin -O2 -mllvm -flexfat-mode=right-align %s -pthread -o %t
// RUN: %t regular reuse-valid
// RUN: not %t regular base free 2>&1 | FileCheck %s --check-prefix=FREE
// RUN: not %t regular before realloc 2>&1 | FileCheck %s --check-prefix=REALLOC
// RUN: not %t regular reuse-old free 2>&1 | FileCheck %s --check-prefix=FREE
// RUN: not %t regular reuse-old realloc-zero 2>&1 | FileCheck %s --check-prefix=REALLOC
// FREE: FLEXFAT ERROR: invalid deallocation
// FREE: operation = free,
// FREE: supplied address = 0x{{[0-9a-f]+}}, expected allocation address = 0x{{[0-9a-f]+}}, slot base = 0x{{[0-9a-f]+}}
// FREE: reason = not allocation start
// REALLOC: FLEXFAT ERROR: invalid deallocation
// REALLOC: operation = realloc,
// REALLOC: supplied address = 0x{{[0-9a-f]+}}, expected allocation address = 0x{{[0-9a-f]+}}, slot base = 0x{{[0-9a-f]+}}
// REALLOC: reason = not allocation start
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);
static uintptr_t raw(uintptr_t p) { return p & 0x00ffffffffffffffULL; }
__attribute__((noinline)) static char *opaque(char *p) {
  asm volatile("" : "+r"(p) : : "memory");
  return p;
}
static void *release(void *p) { free(p); return nullptr; }

int main(int argc, char **argv) {
  assert(argc >= 3);
  const char *layout = argv[1], *position = argv[2];
  char *p = nullptr;
  if (!strcmp(layout, "aligned")) {
    void *q = nullptr;
    assert(!posix_memalign(&q, 256, 273));
    p = opaque((char *)q);
    assert(!(raw((uintptr_t)p) % 256));
  } else {
    assert(!strcmp(layout, "regular"));
    p = opaque((char *)malloc(273));
  }
  assert(p && ((uintptr_t)p >> 56));
  uintptr_t base = __flexfat_get_base((uintptr_t)p);
  uintptr_t size = __flexfat_get_size((uintptr_t)p);

  // Interior accesses remain valid, including aligned/right-aligned objects.
  *(volatile char *)(p + 1) = 17;
  *(volatile char *)(p + 256) = 29;
  assert(*(volatile char *)(p + 1) == 17);
  assert(*(volatile char *)(p + 256) == 29);
  if (!strcmp(position, "valid")) {
    char *q = (char *)realloc(p, 1025);
    assert(q && q[1] == 17 && q[256] == 29);
    free(q);
    free(nullptr);
    q = (char *)realloc(nullptr, 23); assert(q); free(q);
    q = (char *)malloc(23); assert(q); assert(!realloc(q, 0));
    return 0;
  }

  char *bad = nullptr;
  if (!strncmp(position, "reuse-", 6)) {
    // Right-align mode: grow within the same class to remove the old offset.
    assert(raw((uintptr_t)p) > raw(base));
    uintptr_t old_offset = raw((uintptr_t)p) - raw(base);
    uintptr_t old_base = raw(base);
    unsigned old_tag = (uintptr_t)p >> 56;
    free(p);
    p = opaque((char *)malloc(size - 1));
    base = __flexfat_get_base((uintptr_t)p);
    assert(raw(base) == old_base);
    assert(((uintptr_t)p >> 56) != old_tag);
    assert(raw((uintptr_t)p) - raw(base) != old_offset);
    // Use the CURRENT generation: a stale tag would only test temporal checks.
    bad = opaque((char *)(base + old_offset));
    if (!strcmp(position, "reuse-valid")) {
      // Check offset refresh in both directions, and free from another thread.
      free(p);
      p = opaque((char *)malloc(273));
      assert(raw(__flexfat_get_base((uintptr_t)p)) == old_base);
      assert(raw((uintptr_t)p) - old_base == old_offset);
      pthread_t t;
      assert(!pthread_create(&t, nullptr, release, p));
      assert(!pthread_join(t, nullptr));
      return 0;
    }
    assert(!strcmp(position, "reuse-old"));
  } else if (!strcmp(position, "after")) {
    bad = opaque(p + 1);
  } else {
    assert(raw((uintptr_t)p) > raw(base));
    if (!strcmp(position, "base")) bad = opaque((char *)base);
    else { assert(!strcmp(position, "before")); bad = opaque(p - 1); }
  }
  assert(__flexfat_get_base((uintptr_t)bad) == base);
  assert(bad != p);
  // The harness checks the actual address values, not merely diagnostic labels.
  fprintf(stderr, "EXPECT supplied=0x%zx expected=0x%zx base=0x%zx\n",
          raw((uintptr_t)bad), raw((uintptr_t)p), raw(base));
  assert(argc == 4);
  if (!strcmp(argv[3], "free")) free(bad);
  else {
    size_t request = 1025;
    if (!strcmp(argv[3], "realloc-zero")) request = 0;
    else if (!strcmp(argv[3], "realloc-failure")) request = SIZE_MAX;
    else assert(!strcmp(argv[3], "realloc"));
    char *q = (char *)realloc(bad, request);
    asm volatile("" : : "r"(q) : "memory");
  }
  return 0; // Must fail explicitly before returning, allocating, or copying.
}
