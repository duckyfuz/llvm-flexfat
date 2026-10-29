// REQUIRES: flexfat-tbi, flexfat-custom-config
// RUN: not %clangxx_flexfat_tbi -fsanitize-flexfat-tbi=shadow -c %s -o %t.o 2>&1 | FileCheck %s --implicit-check-not='PLEASE submit a bug report'
// RUN: not %clangxx_flexfat_tbi -fsanitize-flexfat-tbi=packed-shadow -c %s -o %t.o 2>&1 | FileCheck %s --check-prefix=PACKED --implicit-check-not='PLEASE submit a bug report'
// RUN: not %clang %flexfat_config_flags -E %S/../../../../lib/flexfat/flexfat_rtl.cpp -o %t.i 2>&1 | FileCheck %s --check-prefix=RUNTIME
// PACKED: FlexFat TBI packed-shadow storage requires a POW2 build
// CHECK: FlexFat TBI shadow storage requires a POW2 build
// RUNTIME: Custom FlexFat TBI requires last-byte or prior-byte storage
int read_byte(char *p) { return *p; }
