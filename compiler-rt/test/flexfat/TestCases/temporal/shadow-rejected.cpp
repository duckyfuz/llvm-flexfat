// REQUIRES: flexfat-tbi, flexfat-custom-config
// RUN: not %clangxx_flexfat_tbi -mllvm -flexfat-tbi-storage=shadow -c %s -o %t.o 2>&1 | FileCheck %s --implicit-check-not='PLEASE submit a bug report'
// CHECK: FlexFat TBI shadow storage requires a POW2 build
int read_byte(char *p) { return *p; }
