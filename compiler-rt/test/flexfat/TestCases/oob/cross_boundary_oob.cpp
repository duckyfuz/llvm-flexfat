// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -O2 %s -o %t && not %run %t 2>&1 | FileCheck %s

extern "C" void *__flexfat_malloc(unsigned long size);

int main() {
  char *buf = (char *)__flexfat_malloc(15);
  if (!buf)
    return 1;

  buf[0] = 'A';
  buf[14] = 'B';

  // CHECK: FLEXFAT ERROR: out-of-bounds error detected!
  double *cross = (double *)(buf + 12);
  *cross = 3.14;

  return 0;
}
