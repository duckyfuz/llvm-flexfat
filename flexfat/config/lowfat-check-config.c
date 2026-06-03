/*
 * FlexFat port of the reference config/lowfat-check-config.c.
 *
 * Validates the glibc-internal pthread offsets the runtime hard-codes
 * (LOWFAT_TID_OFFSET / LOWFAT_JOINID_OFFSET, from the generated config) against
 * the HOST glibc's actual struct pthread layout. These offsets are a landmine:
 * Part II's stack/thread support reads the TID and join-id at these byte offsets
 * inside an opaque pthread_t, so a glibc version whose layout differs silently
 * breaks it. Run this on the target glibc BEFORE trusting thread support.
 *
 * The offsets come from the committed config so we validate what the runtime
 * actually uses.  Build, e.g.:
 *   cc -I golden/nonpow2 -Wno-unused-function -Wno-unused-variable \
 *      -o lowfat-check-config lowfat-check-config.c -lpthread
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "lowfat_config.c" /* provides LOWFAT_TID_OFFSET / LOWFAT_JOINID_OFFSET */

void *worker(void *arg) {
  pthread_t thread = pthread_self();
  pid_t tid = syscall(SYS_gettid);
  pid_t *tid_ptr = (pid_t *)((uint8_t *)thread + LOWFAT_TID_OFFSET);
  if (tid != *tid_ptr) {
    fprintf(stderr, "error: thread-id offset (0x%x) is wrong! (found %d, "
                    "expected %d)\n",
            LOWFAT_TID_OFFSET, *tid_ptr, tid);
    exit(EXIT_FAILURE);
  }
  while (true)
    sleep(1);
  return NULL;
}

int main(int argc, char **argv) {
  pthread_t thread;
  int err = pthread_create(&thread, NULL, worker, NULL);
  if (err != 0) {
    fprintf(stderr, "error: failed to create a thread (err=%d)\n", err);
    exit(EXIT_FAILURE);
  }
  err = pthread_detach(thread);
  if (err != 0) {
    fprintf(stderr, "error: failed to detach thread (err=%d)\n", err);
    exit(EXIT_FAILURE);
  }
  pthread_t *joinid_ptr =
      (pthread_t *)((uint8_t *)thread + LOWFAT_JOINID_OFFSET);
  if (*joinid_ptr != thread) {
    fprintf(stderr, "error: joinid offset (0x%x) is wrong!\n",
            LOWFAT_JOINID_OFFSET);
    exit(EXIT_FAILURE);
  }

  printf("OK\n");
  return 0;
}
