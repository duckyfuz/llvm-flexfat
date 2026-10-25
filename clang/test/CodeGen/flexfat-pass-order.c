// REQUIRES: aarch64-registered-target

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=OPT
// OPT: Running pass: FlexFatSanitizerPass
// OPT-NOT: Running pass: FlexFatSanitizerPass
// OPT: Running pass: InlinerPass
// OPT-NOT: Running pass: FlexFatSanitizerFunctionPass
// OPT: Running pass: FlexFatSanitizerPass
// OPT: Running pass: EarlyCSEPass
// OPT: Running pass: InstCombinePass
// OPT: Running pass: SimplifyCFGPass
// OPT-NOT: Running pass: FlexFatSanitizerPass

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=thin -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LTO
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=full -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LTO
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: EarlyCSEPass
// LTO: Running pass: InstCombinePass
// LTO: Running pass: SimplifyCFGPass

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=O0
// O0: Running pass: FlexFatSanitizerPass
// O0: Running pass: FlexFatSanitizerPass
// O0-NOT: Running pass: EarlyCSEPass
// O0-NOT: Running pass: InstCombinePass
// O0-NOT: Running pass: SimplifyCFGPass

int load_value(int *p) { return *p; }
