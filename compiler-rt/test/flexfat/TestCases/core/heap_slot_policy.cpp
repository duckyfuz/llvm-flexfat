// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O2 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat_right_align -O3 %s -o %t && %run %t | FileCheck %s

#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

extern "C" uintptr_t __flexfat_get_base(uintptr_t);
extern "C" uintptr_t __flexfat_get_size(uintptr_t);
extern "C" void *__flexfat_malloc(size_t);
extern "C" void __flexfat_free(void *);

static char *volatile Saved;

__attribute__((noinline)) static size_t opaque(size_t N) {
  asm volatile("" : "+r"(N));
  return N;
}

__attribute__((noinline)) static void consume(char *P) {
  asm volatile("" : : "r"(P) : "memory");
}

static void check(void *P, size_t N, size_t Alignment) {
  assert(P);
  uintptr_t Address = reinterpret_cast<uintptr_t>(P);
  uintptr_t Base = __flexfat_get_base(Address);
  uintptr_t Size = __flexfat_get_size(Address);
  assert(Base && Address % Alignment == 0);
  assert(Address - Base + N < Size);
  char *End = static_cast<char *>(P) + opaque(N);
  Saved = End;
  char *Reloaded = Saved;
  assert(Reloaded == End);
  consume(Reloaded);
}

int main() {
  // Discover boundaries from this runtime, including non-power-of-two classes.
  size_t Boundary = 1;
  for (unsigned I = 0; I != 12; ++I) {
    void *Probe = malloc(opaque(Boundary));
    assert(Probe);
    Boundary = __flexfat_get_size(reinterpret_cast<uintptr_t>(Probe));
    free(Probe);
    for (size_t N : {Boundary - 1, Boundary, Boundary + 1}) {
      void *P = malloc(opaque(N));
      check(P, N, alignof(max_align_t));
      size_t Actual = __flexfat_get_size(reinterpret_cast<uintptr_t>(P));
      assert(N < Boundary ? Actual == Boundary : Actual > Boundary);
      memset(P, 0x5a, N);
      free(P);
      // Exercise free-list reuse with the same class and potentially new bias.
      P = malloc(opaque(N));
      check(P, N, alignof(max_align_t));
      free(P);
      P = calloc(opaque(N), 1);
      check(P, N, alignof(max_align_t));
      assert(static_cast<char *>(P)[N - 1] == 0);
      free(P);
      for (size_t Alignment : {size_t(16), size_t(64), size_t(4096)}) {
        P = nullptr;
        assert(posix_memalign(&P, Alignment, opaque(N)) == 0);
        check(P, N, Alignment);
        free(P);
      }
    }
  }
  // The direct managed API normalizes zero and rejects arithmetic overflow.
  void *P = __flexfat_malloc(opaque(0));
  check(P, 1, alignof(max_align_t));
  __flexfat_free(P);
  assert(__flexfat_malloc(opaque(SIZE_MAX)) == nullptr);
  P = nullptr;
  assert(posix_memalign(&P, 64, opaque(0)) == 0);
  check(P, 1, 64);
  free(P);
  P = malloc(opaque(0));
  free(P);
  P = calloc(opaque(0), opaque(8));
  free(P);

  char *Old = static_cast<char *>(malloc(opaque(31)));
  memset(Old, 0x5a, 31);
  char *New = static_cast<char *>(realloc(Old, opaque(129)));
  check(New, 129, alignof(max_align_t));
  for (unsigned I = 0; I != 31; ++I)
    assert(New[I] == 0x5a);
  assert(realloc(New, opaque(SIZE_MAX)) == nullptr);
  for (unsigned I = 0; I != 31; ++I)
    assert(New[I] == 0x5a);
  free(New);
  // Discover the maximum class without relying on a particular layout.
  size_t Maximum = 0;
  for (size_t N = 1; N && N <= SIZE_MAX / 2; N *= 2) {
    P = __flexfat_malloc(opaque(N));
    if (!P)
      break;
    Maximum = __flexfat_get_size(reinterpret_cast<uintptr_t>(P));
    __flexfat_free(P);
  }
  assert(Maximum);
  assert(__flexfat_malloc(opaque(Maximum)) == nullptr);
  P = malloc(opaque(Maximum));
  assert((P && __flexfat_get_base(reinterpret_cast<uintptr_t>(P)) == 0) ||
         (!P && errno == ENOMEM));
  free(P);
  P = nullptr;
  int Result = posix_memalign(&P, 4096, opaque(Maximum));
  assert(Result == 0 || Result == ENOMEM);
  assert(reinterpret_cast<uintptr_t>(P) % 4096 == 0);
  assert(__flexfat_get_base(reinterpret_cast<uintptr_t>(P)) == 0);
  free(P);
  P = nullptr;
  assert(posix_memalign(&P, 4096, opaque(SIZE_MAX)) != 0);
  assert(P == nullptr);
  puts("heap slot policy: ok");
}
// CHECK: heap slot policy: ok
