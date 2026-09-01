// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O3 %s -o %t && %run %t | FileCheck %s

#include <cstdio>
#include <cstring>

int main() {
  char dst = 0;
  void *(*copy)(void *, const void *, size_t) = std::memcpy;
  copy(&dst, nullptr, 0);
  std::puts("zero-length memcpy passed");
  return 0;
}

// CHECK: zero-length memcpy passed
