// REQUIRES: aarch64-registered-target
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-tbi=off -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-tbi=last-byte -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-check-whole-access -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fno-sanitize-flexfat-check-whole-access -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-share-contained-geometry -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fno-sanitize-flexfat-share-contained-geometry -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// DEPENDENCY: only allowed with '-fsanitize=flexfat'

// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-share-contained-geometry -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=ACTIVE
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fno-sanitize-flexfat-share-contained-geometry -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=ACTIVE
// ACTIVE: only allowed with '-fsanitize-flexfat-tbi=<active mode>'

// RUN: not %clang_cc1 -triple x86_64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang_cc1 -triple aarch64_be-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang_cc1 -triple aarch64-apple-darwin -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang_cc1 -triple aarch64-linux-gnu_ilp32 -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -target-abi ilp32 -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=TARGET
// TARGET: unsupported option '-fsanitize-flexfat-tbi=last-byte' for target

// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=invalid -emit-llvm -o %t.ll %s 2>&1 | FileCheck %s --check-prefix=INVALID
// INVALID: invalid value 'invalid' in '-fsanitize-flexfat-tbi=invalid'

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=off -fsyntax-only %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-check-whole-access -fno-sanitize-flexfat-check-whole-access -fsyntax-only %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=off -fsyntax-only %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=off -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-share-contained-geometry -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fno-sanitize-flexfat-share-contained-geometry -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s

// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize-flexfat-tbi-ordinary-loads -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED-LOADS
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fno-sanitize-flexfat-tbi-ordinary-loads -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED-LOADS
// REMOVED-LOADS: unknown argument:

int read_value(volatile int *p) { return *p; }
