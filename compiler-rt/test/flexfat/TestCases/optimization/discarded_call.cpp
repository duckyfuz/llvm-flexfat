// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -DNOINLINE_READ -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CATCH
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -DNOINLINE_READ -O3 %s -o %t && %run %t | FileCheck %s --check-prefix=MISS
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -DINLINE_READ -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CATCH
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -DINLINE_READ -O3 %s -o %t && %run %t | FileCheck %s --check-prefix=MISS
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -DSTORE -O0 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CATCH
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -DSTORE -O3 %s -o %t && %run %t | FileCheck %s --check-prefix=MISS
// RUN: %clangxx_flexfat -fsanitize-flexfat-check-whole-access -DLIVE_READ -O3 %s -o %t && not %run %t 2>&1 | FileCheck %s --check-prefix=CATCH

// OptimizerLastEP sees live accesses, but discarded accesses can disappear first.
#include <cstdio>
#include <cstdlib>

#if defined(STORE)
__attribute__((noinline)) static void access(char *p) {
  *reinterpret_cast<double *>(p + 14) = 42.0;
}
#else
#if defined(NOINLINE_READ) || defined(LIVE_READ)
__attribute__((noinline))
#endif
#if !defined(LIVE_READ)
static
#endif
double access(char *p) {
  return *reinterpret_cast<double *>(p + 14);
}
#endif

int main() {
  char *p = static_cast<char *>(malloc(15));
#if defined(LIVE_READ)
  printf("value=%f\n", access(p));
#else
  access(p);
#endif
  free(p);
  puts("DONE");
}
// MISS: DONE
// CATCH: FLEXFAT ERROR: out-of-bounds error detected!
