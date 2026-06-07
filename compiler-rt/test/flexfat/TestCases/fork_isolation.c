// FlexFat Unit 14b: parent/child stack isolation across fork(). The 12a
// hazard surfaces here empirically — without the fork interposer the
// stack regions are MAP_SHARED to one shm fd, so bare fork() leaves
// parent and child looking at the SAME physical stack bytes. Child
// either reads the parent's pre-/post-fork stack writes (aliasing), or
// segfaults on the first instruction after `clone()` because parent and
// child are racing to touch the same stack words.
//
// The test:
//   1. Parent has a stack-local `sentinel = 0xAAAAAAAA`.
//   2. fork().
//   3. Parent writes `sentinel = 0xBBBBBBBB`, signals child via a pipe.
//   4. Child waits on the pipe, then reads `sentinel`, sends value back.
//   5. Parent reaps; checks (a) child exited 0, (b) observed == 0xAAAAAAAA.
//
// With interposer (14b): observed == 0xAAAAAAAA  -> exit 0 (GREEN).
// Without (against 14a runtime): observed == 0xBBBBBBBB (aliased) OR
//   child died from SIGSEGV stomping its own stack -> nonzero (RED).
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: %run %t

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Force `sentinel`'s address to escape so the FlexFat -O2 pipeline keeps
// the volatile stores backed by an actual stack alloca. A volatile local
// whose address is NEVER taken can be eliminated by a downstream pass on
// the FlexFat -O2 pipeline (pre-existing Unit 7/12b interaction outside
// this unit's scope; recorded as a finding).
static unsigned int *volatile sentinel_escape_holder;

int main(void) {
  volatile unsigned int sentinel = 0xAAAAAAAAu;
  sentinel_escape_holder = (unsigned int *)&sentinel;
  int p2c[2], c2p[2];
  if (pipe(p2c) || pipe(c2p)) {
    perror("pipe");
    return 10;
  }

  pid_t pid = fork();
  if (pid < 0) {
    perror("fork");
    return 11;
  }
  if (pid == 0) {
    close(p2c[1]);
    close(c2p[0]);
    char dummy;
    if (read(p2c[0], &dummy, 1) != 1)
      _exit(20);
    unsigned int observed = sentinel;
    if (write(c2p[1], &observed, sizeof(observed)) != (ssize_t)sizeof(observed))
      _exit(21);
    _exit(0);
  }

  /* Parent */
  close(p2c[0]);
  close(c2p[1]);
  sentinel = 0xBBBBBBBBu; /* would alias child's `sentinel` without 14b */
  if (write(p2c[1], "x", 1) != 1)
    return 12;
  unsigned int observed = 0;
  if (read(c2p[0], &observed, sizeof(observed)) != (ssize_t)sizeof(observed))
    return 13;
  int status;
  if (waitpid(pid, &status, 0) < 0)
    return 14;

  if (!WIFEXITED(status)) {
    fprintf(stderr,
            "child did not exit normally: status=0x%x "
            "(WIFSIGNALED=%d WTERMSIG=%d) — bare-fork aliasing hazard\n",
            status, WIFSIGNALED(status),
            WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return 15;
  }
  if (WEXITSTATUS(status) != 0) {
    fprintf(stderr, "child reported pipe failure: exit=%d\n",
            WEXITSTATUS(status));
    return 16;
  }
  if (observed != 0xAAAAAAAAu) {
    fprintf(stderr,
            "ALIASED: child observed sentinel=0x%08x (parent wrote 0xBBBBBBBB);"
            " parent and child share physical stack bytes — the 12a hazard\n",
            observed);
    return 17;
  }
  printf("isolated: child saw 0xAAAAAAAA (parent's write not visible)\n");
  return 0;
}
