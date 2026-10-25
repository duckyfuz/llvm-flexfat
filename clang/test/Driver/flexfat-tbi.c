// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=off %s 2>&1 | FileCheck %s --check-prefix=OFF
// OFF: "-fsanitize-flexfat-tbi=off"
// OFF: libclang_rt.flexfat.a
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte %s 2>&1 | FileCheck %s --check-prefix=LAST_BYTE
// LAST_BYTE: "-fsanitize-flexfat-tbi=last-byte"
// LAST_BYTE: libclang_rt.flexfat_tbi_last_byte.a
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=prior-byte %s 2>&1 | FileCheck %s --check-prefix=PRIOR_BYTE
// PRIOR_BYTE: "-fsanitize-flexfat-tbi=prior-byte"
// PRIOR_BYTE: libclang_rt.flexfat_tbi_prior_byte.a
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=shadow %s 2>&1 | FileCheck %s --check-prefix=SHADOW
// SHADOW: "-fsanitize-flexfat-tbi=shadow"
// SHADOW: libclang_rt.flexfat_tbi.a
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=packed-shadow %s 2>&1 | FileCheck %s --check-prefix=PACKED_SHADOW
// PACKED_SHADOW: "-fsanitize-flexfat-tbi=packed-shadow"
// PACKED_SHADOW: libclang_rt.flexfat_tbi_packed_shadow.a
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=off %s 2>&1 | FileCheck %s --check-prefix=OFF
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=off -fsanitize-flexfat-tbi=prior-byte %s 2>&1 | FileCheck %s --check-prefix=PRIOR_BYTE
// RUN: not %clang -target x86_64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang -target aarch64_be-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang -target arm64-apple-darwin -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=TARGET
// RUN: not %clang -target aarch64-linux-gnu_ilp32 -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=TARGET
// TARGET: unsupported option '-fsanitize-flexfat-tbi=last-byte' for target
// RUN: not %clang -target aarch64-linux-gnu -fsanitize-flexfat-tbi=off -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang -target aarch64-linux-gnu -fsanitize-flexfat-tbi=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang -target aarch64-linux-gnu -fsanitize-flexfat-check-whole-access -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang -target aarch64-linux-gnu -fno-sanitize-flexfat-check-whole-access -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// RUN: not %clang -target aarch64-linux-gnu -fsanitize-flexfat-share-contained-geometry -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=DEPENDENCY
// DEPENDENCY: only allowed with '-fsanitize=flexfat'
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-share-contained-geometry -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=ACTIVE
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fno-sanitize-flexfat-share-contained-geometry -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=ACTIVE
// ACTIVE: only allowed with '-fsanitize-flexfat-tbi=<active mode>'
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=invalid -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=INVALID
// INVALID: unsupported argument 'invalid' to option '-fsanitize-flexfat-tbi='
// RUN: not %clang -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang -fno-sanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang -fsanitize-flexfat-check-whole-access=true -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang -fno-sanitize-flexfat-tbi=off -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang -fsanitize-flexfat-tbi-storage=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// REMOVED: unknown argument:
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LLVM-REMOVED
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=true -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LLVM-REMOVED
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi-storage=last-byte -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LLVM-REMOVED
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi-last-byte -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LLVM-REMOVED
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi-prior-byte -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LLVM-REMOVED
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-check-whole-access -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LLVM-REMOVED
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-share-contained-geometry -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=LLVM-REMOVED
// LLVM-REMOVED: Unknown command line argument

// RUN: not %clang -target aarch64-linux-gnu -fsanitize-flexfat-tbi-ordinary-loads -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED-LOADS
// RUN: not %clang -target aarch64-linux-gnu -fno-sanitize-flexfat-tbi-ordinary-loads -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED-LOADS
// REMOVED-LOADS: unknown argument:

int main(void) { return 0; }
