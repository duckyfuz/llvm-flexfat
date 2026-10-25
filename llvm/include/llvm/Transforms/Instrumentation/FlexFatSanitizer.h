//===- FlexFatSanitizer.h - FlexFat Pointer Bounds Checking -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFATSANITIZER_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFATSANITIZER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Transforms/Instrumentation/FlexFatSanitizerOptions.h"

namespace llvm {
class Module;

struct FlexFatSanitizerOptions {
  FlexFatTBIMode TBI = FlexFatTBIMode::Off;
  bool CheckWholeAccess = false;
  bool ShareContainedGeometry = true;
  bool TBIOrdinaryLoads = false;

  bool hasTBI() const { return TBI != FlexFatTBIMode::Off; }

  bool InternalSkipOptimizations_ = false;
  bool InternalModuleSetupOnly_ = false;
};

/// Emit FlexFat checks. Clang schedules this at OptimizerLastEP and follows
/// it with EarlyCSE, InstCombine, and SimplifyCFG when optimization is enabled.
class FlexFatSanitizerPass : public PassInfoMixin<FlexFatSanitizerPass> {
public:
  LLVM_ABI
  FlexFatSanitizerPass(const FlexFatSanitizerOptions &Options);
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  FlexFatSanitizerOptions Options;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFATSANITIZER_H
