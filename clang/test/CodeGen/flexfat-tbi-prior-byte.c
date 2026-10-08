// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-prior-byte -O2 -emit-llvm -o - %s | FileCheck %s
// CHECK: @__flexfat_tbi_zero_sentinel = external {{.*}}global i8
// CHECK-LABEL: define {{.*}} @read_wide(
// CHECK: flexfat.metadata.address
// CHECK: call void @__flexfat_report_oob
// CHECK: load atomic i8
// CHECK: load volatile i32
// CHECK: call void @__flexfat_tbi_abi_prior_byte_{{pow2_v3|custom_v2}}()
int read_wide(volatile int *p) { return *p; }
