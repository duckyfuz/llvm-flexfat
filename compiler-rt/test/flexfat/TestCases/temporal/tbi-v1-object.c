// REQUIRES: flexfat-tbi
// RUN: %clang -O2 -fno-builtin -ffunction-sections -fdata-sections -c %s -o %t.o
// RUN: not %clangxx_flexfat_tbi %t.o -Wl,--gc-sections -o %t 2>&1 | FileCheck %s
// CHECK: undefined {{.*}}__flexfat_tbi_abi_v1

extern void __flexfat_tbi_abi_v1(void);
__attribute__((constructor(101), used)) static void old_ctor(void) {
  __flexfat_tbi_abi_v1();
}
int main(void) { return 0; }
