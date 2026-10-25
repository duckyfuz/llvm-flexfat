// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t | FileCheck %s

#include <cassert>
#include <cstdio>
#include <cstring>

// Volatile dispatch keeps zero-length calls from disappearing before interception.
static void *(*volatile copy_bytes)(void *, const void *, size_t) = std::memcpy;
static void *(*volatile move_bytes)(void *, const void *, size_t) = std::memmove;
static void *(*volatile set_bytes)(void *, int, size_t) = std::memset;

int main() {
  char dst = 7;
  assert(copy_bytes(&dst, nullptr, 0) == &dst);
  assert(move_bytes(&dst, nullptr, 0) == &dst);
  assert(set_bytes(&dst, 0, 0) == &dst);
  assert(copy_bytes(nullptr, &dst, 0) == nullptr);
  assert(move_bytes(nullptr, &dst, 0) == nullptr);
  assert(set_bytes(nullptr, 0, 0) == nullptr);
  assert(dst == 7);
  puts("zero-length memory operations passed");
}
// CHECK: zero-length memory operations passed
