// FlexFat: the static bounds analysis must recognize every pointer producer in
// real -O2 IR. If getPtrBounds hits an unrecognized producer it falls back to
// NONFAT (silent elide in the reference) -- we instead emit a visible "(BUG)
// unknown pointer type" warning and bump NumUnknownProducers. This canary
// compiles a producer-diverse program through -fsanitize=flexfat at -O2 and
// asserts the fallback never fires (the NumUnknownProducers == 0 corpus
// assertion). A trip here means a missing case to add to getPtrBounds, surfaced
// as a test failure instead of an unsound elision -- more likely on LLVM 23 /
// opaque pointers, where IR forms have shifted from the 4.0 reference.
//
// RUN: %clang_flexfat -O2 -S %s -o /dev/null 2>&1 | FileCheck %s --allow-empty
// CHECK-NOT: unknown pointer

#include <stdlib.h>
#include <string.h>

struct N {
  struct N *next;
  int v;
};

long work(struct N *h, int *a, int n, char *s, int **pp) {
  long t = 0;
  for (struct N *p = h; p; p = p->next) // load-derived chain (load, gep)
    t += p->v;
  for (int i = 0; i < n; i++) // dynamic gep off an argument
    t += a[i];
  int *b = malloc(n * sizeof(int)); // allocation call
  for (int i = 0; i < n; i++)
    b[i] = a[i];
  t += b[0];
  free(b);
  int *q = *pp; // pointer loaded from memory
  t += q[n & 7];
  t += (long)strlen(s);
  int local[16]; // const-size alloca
  for (int i = 0; i < 16; i++)
    local[i] = i;
  return t + local[3];
}

int main(void) { return 0; }
