// FlexFat pass scheduling (Unit 6). The LowFat reference registered its pass at
// EP_ScalarOptimizerLate + EP_EnabledOnOptLevel0, so the checks run after
// mem2reg and stay visible to the rest of the optimizer (which is what keeps
// them cheap), at every optimization level. The new-PM analog is the
// ScalarOptimizerLate extension point, which fires at both -O0 and -O1+.
//
// NOTE on inliner ordering: EP_ScalarOptimizerLate is a *post-inline* point in
// both the legacy PM (the reference adds the main Inliner, then
// addFunctionSimplificationPasses, which hosts EP_ScalarOptimizerLate) and the
// new PM (it lives in the function-simplification pipeline run inside the CGSCC
// inliner). So FlexFat correctly runs *after* the main inliner -- it is not a
// pre-inline pass. The reference's addLowFatPass also bundles a *local* inliner
// immediately after LowFat (clang-4.0 BackendUtil.cpp: "Inline LowFat
// instrumentation") purely to inline the helper CALLS it inserts; the FlexFat
// instrumentation will instead emit its fast-path checks as inline IR (no
// out-of-line call), so that local inliner is unnecessary. See docs/STATUS.md.
//
// REQUIRES: x86-registered-target
//
// At -O2: SROA (mem2reg) -> the CGSCC InlinerPass -> FlexFat at the
// scalar-optimizer-late slot (i.e. FlexFat runs after the main inliner).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=flexfat -O2 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=O2
// O2: Running pass: SROAPass
// O2: Running pass: InlinerPass
// O2: Running pass: FlexFatPass
//
// At -O0: FlexFat still runs (the EP_EnabledOnOptLevel0 property).
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=flexfat -O0 \
// RUN:   -fdebug-pass-manager -emit-llvm -o /dev/null %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=O0
// O0: Running pass: FlexFatPass

void f(int *p) { *p = 0; }
