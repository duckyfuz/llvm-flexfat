// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=off -fsanitize-flexfat-temporal=tagged %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsanitize-flexfat-temporal=off %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: not %clang -target aarch64-linux-gnu -fsanitize-flexfat-temporal=tagged -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fno-sanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not %clang -target x86_64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: not %clang -target aarch64_be-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: not %clang -target arm64-apple-darwin -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: not %clang -target aarch64-linux-gnu_ilp32 -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// TBI: "-fsanitize-flexfat-temporal=tagged"
// TBI-NOT: libclang_rt.flexfat.a
// TBI: libclang_rt.flexfat_tbi{{(-aarch64)?}}.a
// TBI-NOT: libclang_rt.flexfat.a
// PLAIN-NOT: "-fsanitize-flexfat-temporal=tagged"
// PLAIN: libclang_rt.flexfat
// PLAIN-NOT: flexfat_tbi
// MISSING: '-fsanitize-flexfat-temporal=tagged' only allowed with '-fsanitize=flexfat'
// BAD: unsupported option '-fsanitize-flexfat-temporal=tagged' for target
int main(void) { return 0; }

// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: %clang -### -target aarch64-linux-gnu -fno-sanitize=flexfat -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsanitize-flexfat-deallocation-check=exact %s 2>&1 | FileCheck %s --check-prefix=EXACT
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsanitize-flexfat-deallocation-check=exact -fsanitize-flexfat-deallocation-check=basic %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -target x86_64-linux-gnu -fsanitize-flexfat-temporal=off -fsanitize-flexfat-deallocation-check=basic -fsyntax-only %s
// RUN: not %clang -fsanitize-flexfat-deallocation-check=exact -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=COMBINATION
// RUN: not %clang -fsanitize-flexfat-temporal=invalid -fsanitize-flexfat-temporal=off -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=VALUE
// RUN: not %clang -fsanitize-flexfat-deallocation-check=invalid -fsanitize-flexfat-deallocation-check=basic -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=VALUE
// RUN: not %clang -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang -fno-sanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// EXACT: "-fsanitize-flexfat-temporal=tagged"
// EXACT-NOT: "-fsanitize-flexfat-deallocation-check=exact"
// EXACT: libclang_rt.flexfat_tbi_exact
// COMBINATION: '-fsanitize-flexfat-deallocation-check=exact' only allowed with '-fsanitize-flexfat-temporal=tagged'
// VALUE: unsupported argument 'invalid' to option
// REMOVED: unknown argument:

// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsanitize-flexfat-deallocation-check=exact -fsanitize-flexfat-temporal=off -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=COMBINATION
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize-flexfat-temporal=off -fsanitize-flexfat-deallocation-check=basic %s 2>&1 | FileCheck %s --check-prefix=NONE
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-temporal=tagged -fsanitize-flexfat-deallocation-check=exact -shared %s 2>&1 | FileCheck %s --check-prefix=DSO
// NONE-NOT: libclang_rt.flexfat
// DSO: "-fsanitize-flexfat-temporal=tagged"
// DSO-NOT: libclang_rt.flexfat
