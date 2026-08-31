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

namespace llvm {
class Module;

struct FlexFatSanitizerOptions {
  bool Recover = false;
  /// Check the complete width of scalar accesses.  The LowFat-compatible
  /// default checks only the pointer position used by the access.
  bool CheckWholeAccess = false;

  enum class Placement {
    ScalarOptimizerLate,
    OptimizerLast,
    OptimizerEarly,
  };
  Placement PassPlacement = Placement::ScalarOptimizerLate;

  enum class FlexFatMode {
    Fast, /// Instrument at the selected placement (scalar-late by default)
    Safe, /// Instrument at PipelineStartEP and again at selected placement
    RightAlign, /// Selected placement + right-align allocations within class
                /// slots to improve detection of right-side (overflow) OOB at
                /// the cost of a blind spot on the left (underflow) side.
  };
  FlexFatMode Mode = FlexFatMode::Fast;

  bool InternalBarrierOnly_ = false;
  bool InternalModuleSetupOnly_ = false;
};

class FlexFatSanitizerPass : public PassInfoMixin<FlexFatSanitizerPass> {
public:
  LLVM_ABI
  FlexFatSanitizerPass(const FlexFatSanitizerOptions &Options);
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  FlexFatSanitizerOptions Options;
};

class FlexFatSanitizerFunctionPass
    : public PassInfoMixin<FlexFatSanitizerFunctionPass> {
public:
  LLVM_ABI explicit FlexFatSanitizerFunctionPass(
      const FlexFatSanitizerOptions &Options);
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  static bool isRequired() { return true; }

private:
  FlexFatSanitizerOptions Options;
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFATSANITIZER_H
