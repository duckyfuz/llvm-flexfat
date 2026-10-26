// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=LAST
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=prior-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PRIOR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=prior-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PRIOR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=prior-byte -fsanitize-flexfat-tbi=last-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=LAST
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=LAST
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=prior-byte -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=PRIOR
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=prior-byte -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=PRIOR
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=prior-byte -fsanitize-flexfat-tbi=last-byte -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=LAST
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=invalid -emit-llvm -o - %s 2>&1 | FileCheck %s --check-prefix=INVALID
// LAST: @__flexfat_tbi_zero_sentinel = external {{.*}}global i8
// LAST-LABEL: define {{.*}} @read_wide(
// LAST: call void @__flexfat_report_oob
// LAST-NEXT: unreachable
// LAST: flexfat.metadata.address
// LAST: load i8
// LAST: load volatile i32
// LAST: call void @__flexfat_tbi_abi_last_byte_{{pow2_v2|custom_v1}}()
// PRIOR: @__flexfat_tbi_zero_sentinel = external {{.*}}global i8
// PRIOR-LABEL: define {{.*}} @read_wide(
// PRIOR: call void @__flexfat_report_oob
// PRIOR-NEXT: unreachable
// PRIOR: flexfat.metadata.address
// PRIOR: load i8
// PRIOR: load volatile i32
// PRIOR: call void @__flexfat_tbi_abi_prior_byte_{{pow2_v3|custom_v2}}()
// INVALID: invalid value 'invalid'
int read_wide(volatile int *p) { return *p; }
