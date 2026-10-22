// RUN: %clangxx_flexfat -O0 %s -o %t && %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -O2 %s -o %t && %run %t 2>&1 | FileCheck %s

// LowFat reuses slot bases without clearing the payload.
// Both requests must remain accessible.

#include <cstdio>
#include <cstdlib>

int main() {
  // First allocation starts at the slot base.
  char *a = (char *)malloc(48);
  if (!a) return 1;
  for (int i = 0; i < 48; i++) a[i] = (char)i;
  free(a);

  // Second allocation also starts at its slot base.
  char *b = (char *)malloc(63);
  if (!b) return 1;

  // Write and read back all 63 bytes — none must trigger OOB.
  for (int i = 0; i < 63; i++) b[i] = (char)(i + 1);
  for (int i = 0; i < 63; i++)
    if (b[i] != (char)(i + 1)) return 2;

  free(b);

  // CHECK: free_list_reuse: ok
  // CHECK-NOT: FLEXFAT ERROR
  printf("free_list_reuse: ok\n");
  return 0;
}
