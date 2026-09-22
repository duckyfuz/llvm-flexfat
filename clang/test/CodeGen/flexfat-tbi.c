// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -O2 -mllvm -flexfat-mode=safe -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -O2 -mllvm -flexfat-mode=right-align -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -O2 -mllvm -flexfat-placement=optimizer-early -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -O2 -mllvm -flexfat-placement=optimizer-last -emit-llvm -o - %s | FileCheck %s
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

// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-temporal=tagged -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-temporal=bad -fsanitize-flexfat-temporal=off -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=VALUE
// RUN: not %clang_cc1 -triple aarch64_be-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang_cc1 -fsanitize-flexfat-deallocation-check=basic -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=DRIVERONLY
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-temporal=tagged -fsanitize-flexfat-temporal=off -emit-llvm %s -o /dev/null
// MISSING: only allowed with '-fsanitize=flexfat'
// VALUE: unsupported argument 'bad' to option
// TARGET: unsupported option
// DRIVERONLY: unknown argument:

// RUN: not %clang_cc1 -triple aarch64-linux-gnu -target-abi ilp32 -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-tbi -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=DRIVERONLY
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fno-sanitize-flexfat-tbi -emit-llvm %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=DRIVERONLY
