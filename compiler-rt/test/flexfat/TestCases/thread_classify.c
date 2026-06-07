// FlexFat Unit 14a: a pthread_created thread takes the address of a local
// and classifies it as stack — proves the pthread_create interposer (a) ran
// the runtime's stack allocator (b) called pthread_attr_setstack with that
// stack so the new thread executes on a lowfat slot.
//
// RUN: %clang_flexfat_runtime -O2 %s -o %t -lpthread
// RUN: %run %t

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

#include <lowfat.h>

static int worker_kind_ok = 0;

static void *worker(void *arg) {
  (void)arg;
  volatile int local = 42;
  const void *p = (const void *)&local;
  // Both must be true for "the worker is on a lowfat stack":
  if (!lowfat_is_ptr(p))
    return NULL;
  if (!lowfat_is_stack_ptr(p))
    return NULL;
  worker_kind_ok = 1;
  return NULL;
}

int main(void) {
  pthread_t t;
  if (pthread_create(&t, NULL, worker, NULL) != 0) {
    fprintf(stderr, "pthread_create failed\n");
    return 2;
  }
  if (pthread_join(t, NULL) != 0) {
    fprintf(stderr, "pthread_join failed\n");
    return 3;
  }
  if (!worker_kind_ok) {
    fprintf(stderr, "&local in worker did not classify as stack\n");
    return 4;
  }
  return 0;
}
