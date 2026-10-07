// REQUIRES: aarch64-registered-target

// By default, FlexFat does module setup at PipelineStartEP (FlexFatSanitizerPass),
// and performs function-level instrumentation at ScalarOptimizerLateEP (after the main inliner).
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT

// Both LTO pre-link pipelines retain the optimized instrumentation and cleanup.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=thin \
// RUN:   -mllvm -flexfat-mode=optimized -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LTO
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 -flto=full \
// RUN:   -mllvm -flexfat-mode=optimized -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LTO
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: FlexFatSanitizerPass
// LTO: Running pass: EarlyCSEPass
// LTO: Running pass: InstCombinePass
// LTO: Running pass: SimplifyCFGPass
// DEFAULT: Running pass: FlexFatSanitizerPass
// DEFAULT: Running pass: InlinerPass
// DEFAULT: Running pass: FlexFatSanitizerFunctionPass

// Safe mode instruments before the inliner and again at scalar-late.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-mode=safe -mllvm -flexfat-alignment=right \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=SAFE
// SAFE: Running pass: FlexFatSanitizerPass
// SAFE: Running pass: FlexFatSanitizerPass
// SAFE: Running pass: InlinerPass
// SAFE: Running pass: FlexFatSanitizerFunctionPass

int load_value(int *p) { return *p; }

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-instrumentation-point=optimizer-last -mllvm -flexfat-post-cleanup=early-cse \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LAST
// LAST: Running pass: FlexFatSanitizerPass
// LAST: Running pass: InlinerPass
// LAST: Running pass: FlexFatSanitizerPass
// LAST: Running pass: EarlyCSEPass
// LAST: Running pass: InstCombinePass
// LAST: Running pass: SimplifyCFGPass

// The optimized preset selects the same placement and cleanup at O2 and O3.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-mode=optimized -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LAST
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O3 \
// RUN:   -mllvm -flexfat-mode=optimized -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LAST

// Explicit placement and cleanup controls override the mode preset.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-mode=optimized -mllvm -flexfat-instrumentation-point=scalar-late \
// RUN:   -mllvm -flexfat-post-cleanup=none -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT

// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 \
// RUN:   -mllvm -flexfat-instrumentation-point=optimizer-last -mllvm -flexfat-post-cleanup=early-cse \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=O0
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 \
// RUN:   -mllvm -flexfat-post-cleanup=early-cse \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=O0
// O0: Running pass: FlexFatSanitizerPass
// O0: Running pass: FlexFatSanitizer{{(Function)?}}Pass
// O0-NOT: Running pass: EarlyCSEPass
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 \
// RUN:   -mllvm -flexfat-mode=optimized -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=O0

// Placement and cleanup overrides also win when supplied before the preset.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-instrumentation-point=scalar-late -mllvm -flexfat-post-cleanup=none \
// RUN:   -mllvm -flexfat-mode=optimized -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// Final assignments to repeated options win.
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-mode=optimized -mllvm -flexfat-mode=fast \
// RUN:   -mllvm -flexfat-post-cleanup=early-cse -mllvm -flexfat-post-cleanup=none \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=DEFAULT
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 \
// RUN:   -mllvm -flexfat-mode=fast -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=O0
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O0 \
// RUN:   -mllvm -flexfat-mode=safe -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=O0
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-mode=invalid -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=INVALID
// INVALID: Cannot find option named 'invalid'
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-extended-geometry-reuse=true -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-temporal-reuse=true -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-temporal-hoist=true -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-loop-profitability=cost -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-version-tbi-loops=true -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=REMOVED
// REMOVED: Unknown command line argument
// RUN: not %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -mllvm -flexfat-post-cleanup=gvn -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=NO-GVN
// NO-GVN: Cannot find option named 'gvn'
