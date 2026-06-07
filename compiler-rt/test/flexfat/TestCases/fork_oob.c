// FlexFat Unit 14b: after a (post-interposer) fork(), the child runs on
// its own private lowfat stack, and a stack OOB inside the child must
// still trap with the exact (stack)-classified report. This pins
// "interposer didn't break stack lowfatification" — the child's stack
// still resolves through the encoding, the bounds check still fires,
// and the report text stays byte-identical to the heap/stack/global e2e
// reports.
//
// RUN: %clang_flexfat -O2 %s -o %t
// RUN: %run %t 2>&1 | FileCheck %s

#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

__attribute__((noinline)) static void scribble(char *p, int i) {
  p[i] = 0x41;
}

int main(void) {
  pid_t pid = fork();
  if (pid == 0) {
    char buf[16];
    scribble(buf, 32 + 1); /* OOB: i = 33 > class size 32 */
    _exit(0);
  }
  int status;
  waitpid(pid, &status, 0);
  return 0;
}

// CHECK: LOWFAT ERROR: out-of-bounds error detected!
// CHECK: operation = write
// CHECK: pointer   = {{.*}} (stack)
// CHECK: size      = 32
