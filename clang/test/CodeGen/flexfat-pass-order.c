// REQUIRES: x86-registered-target

// By default, FlexFat does module setup at PipelineStartEP (FlexFatSanitizerPass),
// and performs function-level instrumentation at ScalarOptimizerLateEP (after the main inliner).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// DEFAULT: Running pass: FlexFatSanitizerPass
// DEFAULT: Running pass: InlinerPass
// DEFAULT: Running pass: FlexFatSanitizerFunctionPass

// The compatibility placement remains available for controlled comparisons.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -mllvm -flexfat-placement=optimizer-last -fdebug-pass-manager \
// RUN:   -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LAST
// LAST: Running pass: FlexFatSanitizerPass

int load_value(int *p) { return *p; }
