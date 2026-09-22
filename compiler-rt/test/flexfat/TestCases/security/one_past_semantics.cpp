// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O0 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O2 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_safe -O3 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O0 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O2 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && %run %t legal 2>&1 | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && %run %t escape 2>&1 | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && not %run %t boundary 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat_right_align -DREQUEST_SIZE=175 -O3 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB

#include <cstdint>
#include <cstdio>

extern "C" size_t __flexfat_get_usable_size(uintptr_t);
extern "C" size_t __flexfat_get_offset(uintptr_t);

#ifndef REQUEST_SIZE
#define REQUEST_SIZE 16
#endif
static char *volatile saved;
#include <cstdlib>
#include <cstring>

__attribute__((noinline)) static void escape(char *p) {
  asm volatile("" : : "r"(p) : "memory");
  std::printf("%p\n", static_cast<void *>(p));
}

__attribute__((noinline)) static size_t opaqueSize(size_t size) {
  asm volatile("" : "+r"(size));
  return size;
}

int main(int argc, char **argv) {
  volatile size_t runtime_size = REQUEST_SIZE;
  size_t size = runtime_size;
  char *p = static_cast<char *>(std::malloc(size));
  if (!p)
    return 1;
  size_t available = __flexfat_get_usable_size((uintptr_t)p);
  char *end = p + opaqueSize(size);
  saved = end;
  end = saved;
  if (argc > 1 && std::strcmp(argv[1], "legal") == 0) {
    bool formed = end != nullptr;
    std::printf("legal one-past: %d\n", formed);
    std::free(p);
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "boundary") == 0) {
    saved = p + opaqueSize(available);
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "beyond") == 0) {
    escape(p + opaqueSize(available) + opaqueSize(1));
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "underflow") == 0) {
    // Cross the slot base, including any left padding in right-align mode.
    escape(p - opaqueSize(__flexfat_get_offset((uintptr_t)p) + 1));
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "range") == 0) {
    char output[16] = {};
    void *(*volatile copy)(void *, const void *, size_t) = std::memcpy;
    copy(output, p + opaqueSize(available - 8), opaqueSize(9));
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "deref") == 0) {
    volatile char value =
        *reinterpret_cast<volatile char *>(p + opaqueSize(available));
    (void)value;
  } else if (argc > 1 && std::strcmp(argv[1], "write") == 0) {
    *reinterpret_cast<volatile char *>(p + opaqueSize(available)) = 1;
  } else {
    escape(end);
  }
  std::free(p);
  return 0;
}

// LEGAL: legal one-past: 1
// ESCAPE: 0x
// OOB: FLEXFAT ERROR: out-of-bounds error detected!
