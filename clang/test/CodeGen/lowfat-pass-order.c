// REQUIRES: x86-registered-target

// By default, LowFat does module setup at PipelineStartEP (LowFatSanitizerPass),
// and performs function-level instrumentation at ScalarOptimizerLateEP (after the main inliner).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=lowfat -O2 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=DEFAULT
// DEFAULT: Running pass: LowFatSanitizerPass
// DEFAULT: Running pass: InlinerPass
// DEFAULT: Running pass: LowFatSanitizerFunctionPass

// The compatibility placement remains available for controlled comparisons.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=lowfat -O2 \
// RUN:   -mllvm -lowfat-placement=optimizer-last -fdebug-pass-manager \
// RUN:   -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s --check-prefix=LAST
// LAST: Running pass: LowFatSanitizerPass

int load_value(int *p) { return *p; }
