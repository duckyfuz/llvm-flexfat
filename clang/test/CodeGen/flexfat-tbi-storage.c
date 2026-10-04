// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=SHADOW
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=last-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=LAST
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=prior-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PRIOR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=last-byte -mllvm -flexfat-tbi-storage=prior-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=PRIOR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-last-byte -mllvm -flexfat-tbi-prior-byte -mllvm -flexfat-tbi-prior-byte=false -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=SHADOW
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-prior-byte -mllvm -flexfat-tbi-storage=last-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=LAST
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=prior-byte -mllvm -flexfat-tbi-last-byte=false -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=SHADOW
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=last-byte -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=LAST
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=prior-byte -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=PRIOR
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=last-byte -mllvm -flexfat-tbi-prior-byte -mllvm -flexfat-tbi-prior-byte=false -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=SHADOW
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=invalid -emit-llvm -o - %s 2>&1 | FileCheck %s --check-prefix=INVALID
// SHADOW: call void @__flexfat_tbi_abi_v{{[34]}}()
// LAST: call void @__flexfat_tbi_abi_last_byte_{{(pow2|custom)}}_v1()
// PRIOR: call void @__flexfat_tbi_abi_prior_byte_{{(pow2|custom)}}_v2()
// INVALID: for the --flexfat-tbi-storage option: Cannot find option named 'invalid'
int read_wide(volatile int *p) { return *p; }
