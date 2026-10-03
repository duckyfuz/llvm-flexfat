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
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=last-byte %s 2>&1 | FileCheck %s --check-prefix=LAST
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=prior-byte %s 2>&1 | FileCheck %s --check-prefix=PRIOR
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=shadow %s 2>&1 | FileCheck %s --check-prefix=SHADOW
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-last-byte %s 2>&1 | FileCheck %s --check-prefix=LAST
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi-storage=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=STORAGE-MISSING
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-tbi-storage=last-byte -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=LLVM-STORAGE-MISSING
// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=invalid -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=STORAGE-BAD
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=last-byte -fsanitize-flexfat-tbi-storage=prior-byte %s 2>&1 | FileCheck %s --check-prefix=PRIOR
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=prior-byte -fsanitize-flexfat-tbi-storage=last-byte %s 2>&1 | FileCheck %s --check-prefix=LAST
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=last-byte -mllvm -flexfat-tbi-prior-byte -mllvm -flexfat-tbi-prior-byte=false %s 2>&1 | FileCheck %s --check-prefix=SHADOW
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-prior-byte -fsanitize-flexfat-tbi-storage=last-byte %s 2>&1 | FileCheck %s --check-prefix=LAST
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-prior-byte -fsanitize-flexfat-tbi-storage=last-byte %s 2>&1 | FileCheck %s --check-prefix=ORDER
// RUN: %clang -### -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -fsanitize-flexfat-tbi-storage=prior-byte -mllvm -flexfat-tbi-storage=shadow %s 2>&1 | FileCheck %s --check-prefix=SHADOW
// ORDER: "-flexfat-tbi-prior-byte"
// ORDER: "-flexfat-tbi-storage=last-byte"
// ORDER: libclang_rt.flexfat_tbi_last_byte
// LAST: "-flexfat-tbi-storage=last-byte"
// LAST: libclang_rt.flexfat_tbi_last_byte
// PRIOR: "-flexfat-tbi-storage=prior-byte"
// PRIOR: libclang_rt.flexfat_tbi_prior_byte
// SHADOW: "-flexfat-tbi-storage=shadow"
// SHADOW: libclang_rt.flexfat_tbi.a
// STORAGE-MISSING: '-fsanitize-flexfat-tbi-storage=' only allowed with '-fsanitize-flexfat-tbi'
// LLVM-STORAGE-MISSING: '-mllvm -flexfat-tbi-storage=last-byte' only allowed with '-fsanitize-flexfat-tbi'
// STORAGE-BAD: unsupported argument 'invalid'
int main(void) { return 0; }
