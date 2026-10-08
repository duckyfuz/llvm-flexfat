// REQUIRES: aarch64-registered-target

// FlexFat sets up the module at PipelineStartEP and instruments only at
// OptimizerLastEP, followed by the fixed cleanup pipeline.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=OPT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -mllvm -flexfat-alignment=right -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=OPT
// OPT: Running pass: FlexFatSanitizerPass
// OPT-NOT: Running pass: FlexFatSanitizerPass
// OPT: Running pass: InlinerPass
// OPT-NOT: Running pass: FlexFatSanitizerFunctionPass
// OPT: Running pass: FlexFatSanitizerPass
// OPT: Running pass: EarlyCSEPass
// OPT: Running pass: InstCombinePass
// OPT: Running pass: SimplifyCFGPass
// OPT-NOT: Running pass: FlexFatSanitizerPass

// Both LTO pre-link pipelines retain instrumentation and cleanup.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=thin -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LTO
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=full -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LTO
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: EarlyCSEPass
// LTO: Running pass: InstCombinePass
// LTO: Running pass: SimplifyCFGPass

// O0 still instruments, but does not run optimization cleanup.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=O0
// O0: Running pass: FlexFatSanitizerPass
// O0: Running pass: FlexFatSanitizerPass
// O0-NOT: Running pass: EarlyCSEPass
// O0-NOT: Running pass: InstCombinePass
// O0-NOT: Running pass: SimplifyCFGPass

// Removed mode and placement/cleanup switches must not silently select another pipeline.
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-mode=fast -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-mode=safe -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-mode=optimized -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-instrumentation-point=scalar-late -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-instrumentation-point=optimizer-last -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-post-cleanup=none -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -fsanitize=flexfat -mllvm -flexfat-post-cleanup=early-cse -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// REMOVED: Unknown command line argument

int load_value(int *p) { return *p; }
