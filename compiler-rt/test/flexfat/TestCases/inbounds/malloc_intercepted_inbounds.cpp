// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

// Basic in-bounds test for malloc-intercepted allocations.

#include <cstdio>
#include <cstdlib>
#include <cstring>

int main() {
  // malloc goes through the FlexFat interceptor and returns a FlexFat pointer.
  char *buf = (char *)malloc(32);
  if (!buf) return 1;

  // In-bounds scalar accesses: first and last byte.
  buf[0] = 'A';
  buf[31] = 'Z';

  // In-bounds memcpy exactly within the allocation.
  const char src[32] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
  memcpy(buf, src, 32);

  // In-bounds memset exactly within the allocation.
  memset(buf, 0, 32);

  free(buf);

  // CHECK: intercepted_inbounds: ok
  // CHECK-NOT: FLEXFAT ERROR
  // CHECK-NOT: FLEXFAT WARNING
  printf("intercepted_inbounds: ok\n");
  return 0;
}
