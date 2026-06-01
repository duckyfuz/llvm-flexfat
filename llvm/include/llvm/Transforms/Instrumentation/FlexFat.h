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
// Unit 1 (scaffolding): FlexFatPass is a structural no-op. It registers under
// the New Pass Manager as `flexfat`, is loadable via `opt -passes=flexfat`, and
// leaves the module unchanged. The real LowFat-style instrumentation (pointer
// encoding, load/store bounds checks, heap/stack/global lowfatification) lands
// in later units.
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFAT_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFAT_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class Module;

/// Public interface to the FlexFat module pass.
class FlexFatPass : public PassInfoMixin<FlexFatPass> {
public:
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFAT_H
