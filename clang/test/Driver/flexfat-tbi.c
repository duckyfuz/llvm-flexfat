// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=true %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=1 %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm --flexfat-tbi=false %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm --flexfat-tbi=0 %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=false -mllvm --flexfat-tbi=true %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=true -mllvm --flexfat-tbi=false %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=false -mllvm -flexfat-tbi=true %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=true -mllvm -flexfat-tbi=false %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: not %clang -target x86_64-linux-gnu -fsanitize=flexfat -mllvm --flexfat-tbi=true -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=true %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi=false %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=false -mllvm -flexfat-tbi=true %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=true -mllvm -flexfat-tbi=false %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: not %clang -target x86_64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi=true -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fno-sanitize-flexfat-tbi -fsanitize-flexfat-tbi %s 2>&1 | FileCheck %s --check-prefix=TBI
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fno-sanitize-flexfat-tbi %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// RUN: not %clang -target aarch64-linux-gnu -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fno-sanitize=flexfat -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=MISSING
// RUN: not %clang -target x86_64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: not %clang -target aarch64_be-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: not %clang -target arm64-apple-darwin -fsanitize=flexfat -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// RUN: not %clang -target aarch64-linux-gnu_ilp32 -fsanitize=flexfat -fsanitize-flexfat-tbi -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=BAD
// TBI: "-fsanitize-flexfat-tbi"
// TBI-NOT: libclang_rt.flexfat.a
// TBI: libclang_rt.flexfat_tbi
// TBI-NOT: libclang_rt.flexfat.a
// PLAIN-NOT: "-fsanitize-flexfat-tbi"
// PLAIN: libclang_rt.flexfat
// PLAIN-NOT: flexfat_tbi
// MISSING: '-fsanitize-flexfat-tbi' only allowed with '-fsanitize=flexfat'
// BAD: unsupported option '-fsanitize-flexfat-tbi' for target
int main(void) { return 0; }
