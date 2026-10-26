// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=off -fsanitize-flexfat-tbi=last-byte -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=off -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PLAIN
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -O0 -emit-llvm -o - %s | FileCheck %s
// PLAIN-NOT: __flexfat_report_temporal_v3
// PLAIN-NOT: __flexfat_tbi_abi
// CHECK: @llvm.global_ctors
// CHECK: @llvm.used
// CHECK-LABEL: define {{.*}} @access(
// CHECK: load i8, ptr {{.*}}, align 1
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
