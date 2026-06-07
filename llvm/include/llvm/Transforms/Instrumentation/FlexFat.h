//===- FlexFat.h - FlexFat bounds-checking instrumentation ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the FlexFat pass, an in-tree reimplementation of the
// LowFat spatial-memory-safety bounds checker (Duck & Yap, NUS), modeled
// structurally on AddressSanitizer.
//
// FlexFatPass is a New-PM *function* pass. The LowFat reference scheduled its
// instrumentation at EP_ScalarOptimizerLate (a per-function extension point) so
// the bounds checks run right after mem2reg and stay visible to the rest of the
// optimizer; reproducing that placement requires a function pass. FlexFat's
// hot-path checks only reference the runtime-provided tables at fixed addresses
// (`_LOWFAT_SIZES`@0x200000, `_LOWFAT_MAGICS`@0x300000) as externals, so the
// per-function instrumentation needs no module-level setup (unlike ASan).
//
// Unit 1/6 (scaffolding + driver wiring): the body is still a structural no-op.
// It registers under the New Pass Manager as `flexfat`, is loadable via
// `opt -passes=flexfat`, runs at the reference's pipeline point, and leaves the
// function unchanged. The real LowFat-style instrumentation (pointer encoding,
// load/store bounds checks, heap/stack/global lowfatification) lands in later
// units.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFAT_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFAT_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Function;
class Module;

/// Public interface to the FlexFat function pass.
class FlexFatPass : public PassInfoMixin<FlexFatPass> {
public:
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  // FlexFat is instrumentation: like the other sanitizers, it must run even on
  // `optnone` functions (which clang attaches to every function at -O0).
  // Without this, the function-pass adaptor would skip it at -O0, defeating the
  // reference's EP_EnabledOnOptLevel0 placement.
  static bool isRequired() { return true; }
};

/// Unit 13: module pass for global-variable lowfatification. Eligible globals
/// (see isInterestingGlobal in FlexFat.cpp — not thread-local, ordinary
/// linkage, no user section/oversized-alignment, size ≤
/// LOWFAT_MAX_GLOBAL_ALLOC_SIZE) are placed in `lowfat_section_<size>` (or
/// `lowfat_section_const_<size>`) with their class-boundary alignment. The
/// driver-applied `lowfat.ld` pins those sections to each region's
/// [16 GiB, 24 GiB) global sub-range, so `&g` lands inside a low-fat region
/// and the Unit-7 bounds check fires through it just like a heap/stack ptr.
class FlexFatGlobalsPass : public PassInfoMixin<FlexFatGlobalsPass> {
public:
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  // Same isRequired() reasoning as the function pass: instrumentation must
  // run at every -O level.
  static bool isRequired() { return true; }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFAT_H
