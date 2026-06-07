// FlexFat Unit 12a: the stack pivot happens before main, so a main that takes
// the address of a local and classifies it must see it as a lowfat stack
// pointer — NOT nonfat. The pass itself is unchanged in 12a (no alloca
// transform yet); this test exercises only the runtime's pivot machinery.
//
// RUN: %clang_flexfat_runtime -O0 %s -o %t
// RUN: %run %t
//
// CHECK is via the program's exit status: any failure path calls exit(1+...).

#include <stdbool.h>
#include <stdio.h>

#include <lowfat.h>

int main(int argc, char **argv) {
  volatile int local = argc;
  const void *p = (const void *)&local;
  bool is_ptr = lowfat_is_ptr(p);
  bool is_stack = lowfat_is_stack_ptr(p);

  // Print so a failure shows what we actually got:
  printf("&local=%p is_ptr=%d is_stack=%d\n", p, (int)is_ptr, (int)is_stack);
  fflush(stdout);

  if (!is_ptr)
    return 2;   // pivot never ran ⇒ &local is on the loader stack (nonfat)
  if (!is_stack)
    return 3;   // pointer is in a low-fat region but not the stack sub-range
  return 0;
}
