// REQUIRES: flexfat-tbi, flexfat-custom-config
// RUN: not %clangxx_flexfat_tbi -mllvm -flexfat-tbi-storage=shadow -c %s -o %t.o 2>&1 | FileCheck %s --implicit-check-not='PLEASE submit a bug report'
// RUN: not %clangxx_flexfat_tbi -mllvm -flexfat-tbi-storage=packed-shadow -c %s -o %t.o 2>&1 | FileCheck %s --check-prefix=PACKED --implicit-check-not='PLEASE submit a bug report'
// PACKED: FlexFat TBI packed-shadow storage requires a POW2 build
// CHECK: FlexFat TBI shadow storage requires a POW2 build
int read_byte(char *p) { return *p; }
