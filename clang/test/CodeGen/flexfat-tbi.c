// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=false -mllvm --flexfat-tbi=true -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=true -mllvm -flexfat-tbi=false -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=true -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=1 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=CHECK
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm --flexfat-tbi=false -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm --flexfat-tbi=0 -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PLAIN
// RUN: not %clang_cc1 -triple x86_64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=true -emit-llvm -o - %s 2>&1 | FileCheck %s --check-prefix=BAD
// BAD: unsupported option '-fsanitize-flexfat-tbi' for target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi=false -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PLAIN
// PLAIN-NOT: __flexfat_check_temporal
// PLAIN-NOT: __flexfat_tbi_abi
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=true -O2 -mllvm -flexfat-mode=safe -mllvm -flexfat-alignment=right -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -O2 -mllvm -flexfat-mode=safe -mllvm -flexfat-alignment=right -emit-llvm -o - %s | FileCheck %s
// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -O2 -mllvm -flexfat-mode=safe -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -O2 -mllvm -flexfat-alignment=right -emit-llvm -o - %s | FileCheck %s
// CHECK: @llvm.global_ctors
// CHECK: @llvm.used
// CHECK-LABEL: define {{.*}} @access(
// CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 4, i32 0)
// CHECK: load volatile i32
int access(volatile int *p) { return *p; }
// CHECK-LABEL: define {{.*}} @excluded(
// CHECK-NOT: call void @__flexfat
// CHECK: ret i32
__attribute__((no_sanitize("flexfat"))) int excluded(int *p) { return *p; }
// CHECK-LABEL: define {{.*}} @disabled(
// CHECK-NOT: call void @__flexfat
// CHECK: ret i32
__attribute__((disable_sanitizer_instrumentation)) int disabled(int *p) { return *p; }
