// RUN: %clangxx_flexfat -O0 %s -o %t
// RUN: not %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-FATAL
// RUN: env FLEXFAT_OPTIONS=allocator_may_return_null=1 %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-NULL

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
  const size_t nmemb = 4096;
  const size_t size = (SIZE_MAX / nmemb) + 10;
  void *p = calloc(nmemb, size);
  fprintf(stderr, "errno: %d, ptr: %lx\n", errno, (unsigned long)p);
  return 0;
}

// CHECK-FATAL: calloc_overflow.cpp
// CHECK-FATAL: SUMMARY: {{.*}}calloc-overflow{{.*}}calloc_overflow.cpp{{.*}} in main

// CHECK-NULL: errno: 12, ptr: 0
