/*
 * FlexFat port of the reference config/lowfat-check-config.c.
 *
 * Validates the glibc-internal pthread offsets the runtime hard-codes
 * (LOWFAT_TID_OFFSET / LOWFAT_JOINID_OFFSET, from the generated config)
 * against the HOST glibc's actual struct pthread layout. These offsets
 * are a landmine: stack/thread support reads the TID and join-id at
 * these byte offsets inside an opaque pthread_t, so a glibc version
 * whose layout differs silently corrupts dead-thread reclamation.
 *
 * Unit 14a: this validator is promoted to a build-time gate (wired in
 * compiler-rt/lib/flexfat/CMakeLists.txt). The runtime archive cannot
 * build until both offsets validate on the build host. Rationale:
 * glibc's descr.h declares `struct pthread` layout PRIVATE and
 * subject to change without ABI notice; we've already observed two
 * different correct JOINID values across glibc versions (0x620 on
 * 2.39, 0x628 on the upstream LowFat pin's target glibc). Without
 * the gate, a host glibc update silently corrupts dead-thread
 * detection — false reclamation of a still-live stack at best,
 * memory-stomp at worst.
 *
 * On mismatch the validator prints the EXPECTED offset, the VALUE
 * FOUND at that offset, and the EXPECTED value, then exits non-zero
 * so the build fails. Rebuild the configured offset (SPEC §III.1
 * recommends disassembling glibc's pthread_detach()) and regenerate.
 *
 * Build via CMake (the standalone command below is preserved for
 * manual / pre-rebuild offset discovery):
 *   cc -I golden/nonpow2 -Wno-unused-function -Wno-unused-variable \
 *      -o lowfat-check-config lowfat-check-config.c -lpthread
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include "lowfat_config.c" /* provides LOWFAT_TID_OFFSET / LOWFAT_JOINID_OFFSET */

/* Worker → main sync. Worker reads the TID-offset slot before signaling,
 * stashes the observed value, then waits forever (reaped at process exit).
 * Main waits on the condvar so the TID check is guaranteed to happen
 * before the JOINID check + OK print — the original validator had a race
 * where main could print OK before the worker had run. */
static pthread_mutex_t worker_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_cv = PTHREAD_COND_INITIALIZER;
static atomic_int worker_ready = 0;
static pid_t worker_observed_tid;
static pid_t worker_real_tid;

void *worker(void *arg) {
  (void)arg;
  pthread_t thread = pthread_self();
  worker_real_tid = (pid_t)syscall(SYS_gettid);
  worker_observed_tid =
      *(pid_t *)((uint8_t *)thread + LOWFAT_TID_OFFSET);
  pthread_mutex_lock(&worker_mu);
  atomic_store(&worker_ready, 1);
  pthread_cond_signal(&worker_cv);
  pthread_mutex_unlock(&worker_mu);
  for (;;)
    sleep(1); /* reaped at process exit */
  return NULL;
}

static void fail_offsets(const char *which, unsigned expected_off,
                         uintptr_t found, uintptr_t want) {
  fprintf(stderr,
          "\n"
          "FlexFat build-gate failure: glibc pthread %s offset does not\n"
          "match the configured constant.\n"
          "\n"
          "  %s_OFFSET: configured 0x%x\n"
          "    value at that offset: 0x%lx\n"
          "    expected value:        0x%lx\n"
          "\n"
          "glibc declares `struct pthread` layout PRIVATE in descr.h and\n"
          "changes it without ABI notice (observed: JOINID 0x620 on glibc\n"
          "2.39, 0x628 on the upstream LowFat pin's target glibc). Without\n"
          "the matching offset, FlexFat's dead-thread reclamation reads the\n"
          "wrong word and silently corrupts stack slot ownership.\n"
          "\n"
          "Find the real offset by disassembling glibc's pthread_detach()\n"
          "(SPEC §III.1) and update LOWFAT_%s_OFFSET in\n"
          "flexfat/config/golden/nonpow2/lowfat_config.c, then regenerate\n"
          "(flexfat/config/test/nonpow2-parity.test pins it).\n"
          "\n",
          which, which, expected_off, found, want, which);
  exit(EXIT_FAILURE);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;
  pthread_t thread;
  int err = pthread_create(&thread, NULL, worker, NULL);
  if (err != 0) {
    fprintf(stderr, "FlexFat build-gate validator: pthread_create failed "
                    "(err=%d). This is unrelated to the offset check.\n",
            err);
    exit(EXIT_FAILURE);
  }

  /* Wait for worker to publish the TID-check result. */
  pthread_mutex_lock(&worker_mu);
  while (!atomic_load(&worker_ready))
    pthread_cond_wait(&worker_cv, &worker_mu);
  pthread_mutex_unlock(&worker_mu);

  if (worker_observed_tid != worker_real_tid)
    fail_offsets("TID", LOWFAT_TID_OFFSET,
                 (uintptr_t)(uint32_t)worker_observed_tid,
                 (uintptr_t)(uint32_t)worker_real_tid);

  err = pthread_detach(thread);
  if (err != 0) {
    fprintf(stderr, "FlexFat build-gate validator: pthread_detach failed "
                    "(err=%d). This is unrelated to the offset check.\n",
            err);
    exit(EXIT_FAILURE);
  }

  pthread_t observed_joinid =
      *(pthread_t *)((uint8_t *)thread + LOWFAT_JOINID_OFFSET);
  if (observed_joinid != thread)
    fail_offsets("JOINID", LOWFAT_JOINID_OFFSET,
                 (uintptr_t)observed_joinid, (uintptr_t)thread);

  printf("OK: TID@0x%x JOINID@0x%x\n", LOWFAT_TID_OFFSET, LOWFAT_JOINID_OFFSET);
  return 0;
}
