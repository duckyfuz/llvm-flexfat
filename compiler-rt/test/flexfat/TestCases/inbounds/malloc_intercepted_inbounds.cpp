// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset -O0 %s -o %t && %run %t | FileCheck %s
// RUN: %clangxx_flexfat -fno-builtin-memcpy -fno-builtin-memmove -fno-builtin-memset -O2 %s -o %t && %run %t | FileCheck %s

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
  char *buf = static_cast<char *>(malloc(32));
  assert(buf);
  volatile char *bytes = buf;
  bytes[0] = 'A';
  bytes[31] = 'Z';
  assert(bytes[0] == 'A' && bytes[31] == 'Z');
  const char src[32] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  memcpy(buf, src, 32);
  assert(bytes[0] == 'A' && bytes[31] == 0);
  memmove(buf + 1, buf, 31);
  assert(bytes[1] == 'A' && bytes[31] == 'A');
  memset(buf, 0, 32);
  for (unsigned i = 0; i != 32; ++i)
    assert(bytes[i] == 0);
  free(buf);
  puts("intercepted_inbounds: ok");
}
// CHECK: intercepted_inbounds: ok
