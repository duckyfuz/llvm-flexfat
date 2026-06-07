// FlexFat Unit 14a: a stack buffer overflow inside a spawned thread traps
// with `pointer = … (stack)` in the report. The escape is through a
// noinline callee so the alloca lowfatification (Unit 12b) inserts the
// mirror, and the bounds check on the mirror fires from inside the
// pthread_created thread (proving the thread's lowfat stack is set up
// correctly — TID/JOINID offsets + per-class mprotect coverage).
//
// Unit 17: variant-specific assertions; gate to non-POW2.
// REQUIRES: flexfat-nonpow2
// RUN: %clang_flexfat -O2 %s -o %t -lpthread
// RUN: not --crash %run %t 2>&1 | FileCheck %s

#include <pthread.h>

__attribute__((noinline)) static void scribble(char *p, int i) {
  p[i] = 0x41;
}

static void *worker(void *arg) {
  (void)arg;
  char buf[16];
  scribble(buf, 32 + 1);  // OOB: i = 33 > class size 32
  return NULL;
}

int main(void) {
  pthread_t t;
  pthread_create(&t, 0, worker, 0);
  pthread_join(t, 0);
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = write
// CHECK: pointer   = {{.*}} (stack)
// CHECK: size      = 32
