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
  bool TemporalTBI = false;
  /// Check the complete width of scalar accesses.  The LowFat-compatible
  /// default checks only the pointer position used by the access.
  bool CheckWholeAccess = false;

  enum class FlexFatMode {
    Fast, /// Instrument at ScalarOptimizerLateEP.
    Safe, /// Instrument at PipelineStartEP and ScalarOptimizerLateEP.
  };
  FlexFatMode Mode = FlexFatMode::Fast;

  enum class Alignment { Left, Right };
  Alignment AllocationAlignment = Alignment::Left;

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
