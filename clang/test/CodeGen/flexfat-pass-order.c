// REQUIRES: aarch64-registered-target

// By default, FlexFat does module setup at PipelineStartEP (FlexFatSanitizerPass),
// and performs function-level instrumentation at ScalarOptimizerLateEP (after the main inliner).
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
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
