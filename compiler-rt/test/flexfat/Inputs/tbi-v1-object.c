// Model the v1 object contract without using the current instrumentation pass.
#include <stdint.h>
#include <stdlib.h>
extern void __flexfat_tbi_abi_v1(void);
extern void __flexfat_check_temporal(uintptr_t, uintptr_t, int);
__attribute__((constructor(101), used)) static void old_ctor(void) {
  __flexfat_tbi_abi_v1();
}
int main(int argc, char **argv) {
  char *p = malloc(23);
  __flexfat_check_temporal((uintptr_t)p, 1, 1);
  *(volatile char *)p = 9;
  if (argc > 1) free(p);
  __flexfat_check_temporal((uintptr_t)p, 1, 0);
  int result = *(volatile char *)p != 9;
  if (argc == 1) free(p);
  return result;
}
