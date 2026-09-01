// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t legal | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t escape | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O0 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t legal | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t escape | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O2 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t legal | FileCheck %s --check-prefix=LEGAL
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t escape | FileCheck %s --check-prefix=ESCAPE
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t beyond 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t deref 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t write 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t range 2>&1 | FileCheck %s --check-prefix=OOB
// RUN: %clangxx_flexfat -O3 %s -o %t && not %run %t underflow 2>&1 | FileCheck %s --check-prefix=OOB

#include <cstdio>
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
  volatile size_t runtime_size = 16;
  size_t size = runtime_size;
  char *p = static_cast<char *>(std::malloc(size));
  if (!p)
    return 1;
  char *end = p + opaqueSize(size);
  if (argc > 1 && std::strcmp(argv[1], "legal") == 0) {
    bool formed = end != nullptr;
    std::printf("legal one-past: %d\n", formed);
    std::free(p);
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "beyond") == 0) {
    escape(p + opaqueSize(size) + opaqueSize(1));
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "underflow") == 0) {
    escape(p - opaqueSize(1));
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "range") == 0) {
    char output[16] = {};
    void *(*volatile copy)(void *, const void *, size_t) = std::memcpy;
    copy(output, p + opaqueSize(8), opaqueSize(9));
    return 0;
  }
  if (argc > 1 && std::strcmp(argv[1], "deref") == 0) {
    volatile char value =
        *reinterpret_cast<volatile char *>(p + opaqueSize(size));
    (void)value;
  } else if (argc > 1 && std::strcmp(argv[1], "write") == 0) {
    *reinterpret_cast<volatile char *>(p + opaqueSize(size)) = 1;
  } else {
    escape(end);
  }
  std::free(p);
  return 0;
}

// LEGAL: legal one-past: 1
// ESCAPE: 0x
// OOB: FLEXFAT ERROR: out-of-bounds error detected!
