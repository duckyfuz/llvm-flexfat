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
// LAST: call void @__flexfat_tbi_abi_last_byte_{{pow2_v2|custom_v1}}()
// PRIOR: call void @__flexfat_tbi_abi_prior_byte_{{pow2_v3|custom_v2}}()
// INVALID: invalid value 'invalid'
int read_wide(volatile int *p) { return *p; }
