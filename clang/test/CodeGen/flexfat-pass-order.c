// FlexFat pass scheduling (Unit 6). The LowFat reference registered its pass at
// EP_ScalarOptimizerLate + EP_EnabledOnOptLevel0, so the checks run right after
// mem2reg and stay visible to the rest of the optimizer (which is what keeps
// them cheap), at every optimization level. The new-PM analog is the
// ScalarOptimizerLate extension point, which fires at both -O0 and -O1+.
//
// REQUIRES: x86-registered-target
//
// At -O2: FlexFat runs after SROA (mem2reg), at the scalar-optimizer-late slot.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=O2
// O2: Running pass: SROAPass
// O2: Running pass: FlexFatPass
//
// At -O0: FlexFat still runs (the EP_EnabledOnOptLevel0 property).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=flexfat -O0 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=O0
// O0: Running pass: FlexFatPass

void f(int *p) { *p = 0; }
