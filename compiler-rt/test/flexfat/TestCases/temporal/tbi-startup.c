// REQUIRES: flexfat-tbi
// RUN: %clang -fno-builtin -c %s -o %t.o
// RUN: %clang %t.o -Wl,--whole-archive %flexfat_tbi_runtime -Wl,--no-whole-archive -lpthread -ldl -lrt -lm -o %t
// RUN: %run %t

// Compile without instrumentation and link the object before the runtime so
// this preinit hook exercises allocation before runtime initialization.
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static char *bootstrap;
static void early(void) {
  bootstrap = malloc(16);
  assert(bootstrap && !((uintptr_t)bootstrap >> 56));
  memset(bootstrap, 42, 16);
  void *p = 0; assert(!posix_memalign(&p, 64, 16)); free(p);
}
__attribute__((section(".preinit_array"), used)) static void (*pre)(void) = early;
int main(void) { assert(bootstrap[0] == 42); free(bootstrap); }
