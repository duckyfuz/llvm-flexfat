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
// Unit 1 (scaffolding): this is a structural no-op New-PM module pass. It
// exists so the pass plumbing (registration, pipeline scheduling, IR tests) can
// be stood up before any real instrumentation is ported. run() preserves all
// analyses and leaves the module unchanged.
//
//===----------------------------------------------------------------------===//

#include "llvm/Transforms/Instrumentation/FlexFat.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

using namespace llvm;

PreservedAnalyses FlexFatPass::run(Module &M, ModuleAnalysisManager &AM) {
  // Unit 1: no instrumentation yet. Leave the module byte-for-byte unchanged.
  return PreservedAnalyses::all();
}
