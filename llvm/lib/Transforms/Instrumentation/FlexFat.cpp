//===- FlexFat.cpp - FlexFat bounds-checking instrumentation --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FlexFat: an in-tree reimplementation of the LowFat spatial-memory-safety
// bounds checker, modeled structurally on AddressSanitizer.
//
// Scaffolding: this is a structural no-op New-PM function pass. It exists so the
// pass plumbing (registration, pipeline scheduling at the reference's
// EP_ScalarOptimizerLate point, IR tests) can be stood up before any real
// instrumentation is ported. run() preserves all analyses and leaves the
// function unchanged.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/FlexFat.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

PreservedAnalyses FlexFatPass::run(Function &F, FunctionAnalysisManager &AM) {
  // No instrumentation yet. Leave the function byte-for-byte unchanged.
  return PreservedAnalyses::all();
}
