// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-recover=true -emit-llvm -o - %s | FileCheck %s --check-prefix=RECOVER
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-recover=flexfat -O2 -mllvm -flexfat-recover=false -emit-llvm -o - %s | FileCheck %s --check-prefix=FATAL
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=FATAL
// RECOVER: call void @__flexfat_set_recover(i32 1)
// FATAL-NOT: __flexfat_set_recover
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=LEFT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-mode=fast -mllvm -flexfat-alignment=left -emit-llvm -o - %s | FileCheck %s --check-prefix=LEFT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-mode=safe -mllvm -flexfat-alignment=left -emit-llvm -o - %s | FileCheck %s --check-prefix=LEFT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-mode=fast -mllvm -flexfat-alignment=right -emit-llvm -o - %s | FileCheck %s --check-prefix=RIGHT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-alignment=right -mllvm -flexfat-mode=safe -emit-llvm -o - %s | FileCheck %s --check-prefix=RIGHT
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-mode=right-align -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=BAD-MODE
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-alignment=invalid -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=BAD-ALIGNMENT
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-placement=scalar-late -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=BAD-PLACEMENT
// LEFT-NOT: __flexfat_set_right_align
// RIGHT: call void @__flexfat_set_right_align(i32 1)
// BAD-MODE: Cannot find option named 'right-align'
// BAD-ALIGNMENT: Cannot find option named 'invalid'
// BAD-PLACEMENT: Unknown command line argument '-flexfat-placement=scalar-late'

int load_value(int *p) { return *p; }
