// FlexFat Unit 14a: multi-thread allocator stress — first real concurrency
// exercise for the Unit-4 per-region mutexes. Several threads concurrently
// malloc/free across multiple size classes; the per-region locking must
// keep the freelists consistent. Deadlock or corruption shows up as
// hang/segv; success is `exit 0` at -O0 and -O2.
//
// RUN: %clang_flexfat -O2 %s -o %t -lpthread
// RUN: %run %t
//
// RUN: %clang_flexfat -O0 %s -o %t.O0 -lpthread
// RUN: %run %t.O0

#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { THREADS = 4, ITERS_PER_THREAD = 2000 };

static void *worker(void *arg) {
  unsigned seed = (unsigned)(uintptr_t)arg;
  // PRNG; not seeded for cryptographic strength, just decorrelated per thread.
  for (int i = 0; i < ITERS_PER_THREAD; i++) {
    size_t size_classes[] = {16, 64, 192, 1024, 8192, 65536};
    size_t pick = size_classes[rand_r(&seed) % 6];
    void *p = malloc(pick);
    if (!p)
      return (void *)1;
    memset(p, (int)(i & 0xFF), pick);
    // Touch a byte to force a real fault if the alloc returned bogus memory.
    if (((volatile char *)p)[pick - 1] != (char)(i & 0xFF))
      return (void *)2;
    free(p);
  }
  return NULL;
}

int main(void) {
  pthread_t ts[THREADS];
  for (int i = 0; i < THREADS; i++)
    if (pthread_create(&ts[i], 0, worker, (void *)(uintptr_t)(i + 1)) != 0)
      return 3;
  for (int i = 0; i < THREADS; i++) {
    void *r;
    if (pthread_join(ts[i], &r) != 0)
      return 4;
    if (r != NULL)
      return 5; // worker reported a fault
  }
  return 0;
}
