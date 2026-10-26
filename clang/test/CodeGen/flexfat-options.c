// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-recover=true -emit-llvm -o - %s | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-recover=flexfat -O2 -mllvm -flexfat-recover=false -emit-llvm -o - %s | FileCheck %s --check-prefix=FATAL
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=FATAL
// RECOVER: call void @__flexfat_set_recover(i32 1)
// FATAL-NOT: __flexfat_set_recover
// REQUIRES: aarch64-registered-target
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-placement=scalar-late -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=BAD-PLACEMENT
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-alignment=left -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED-LEFT
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-alignment=right -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED-RIGHT
// REMOVED-LEFT: Unknown command line argument '-flexfat-alignment=left'
// REMOVED-RIGHT: Unknown command line argument '-flexfat-alignment=right'
// BAD-PLACEMENT: Unknown command line argument '-flexfat-placement=scalar-late'

int load_value(int *p) { return *p; }
