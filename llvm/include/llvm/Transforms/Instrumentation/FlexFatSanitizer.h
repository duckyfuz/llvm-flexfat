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
  enum class TBIStorage { Shadow, LastByte, PriorByte };
  TBIStorage Storage = TBIStorage::LastByte;
  /// Check the complete width of scalar accesses.  The LowFat-compatible
  /// default checks only the pointer position used by the access.
  bool CheckWholeAccess = false;

  enum class FlexFatMode {
    Fast, /// Instrument at the selected later extension point.
    Safe, /// Instrument at PipelineStartEP and the selected later point.
    Optimized, /// Instrument at OptimizerLastEP, then clean up generated IR.
  };
  FlexFatMode Mode = FlexFatMode::Fast;

  enum class Alignment { Left, Right };
  Alignment AllocationAlignment = Alignment::Left;

  enum class InstrumentationPoint { ScalarLate, OptimizerLast };
  InstrumentationPoint Point = InstrumentationPoint::ScalarLate;
  enum class PostCleanup { None, EarlyCSE };
  PostCleanup Cleanup = PostCleanup::None;
  /// Apply a mode preset before explicit placement and cleanup overrides.
  void setMode(FlexFatMode NewMode) {
    Mode = NewMode;
    Point = InstrumentationPoint::ScalarLate;
    Cleanup = PostCleanup::None;
    if (Mode == FlexFatMode::Optimized) {
      Point = InstrumentationPoint::OptimizerLast;
      Cleanup = PostCleanup::EarlyCSE;
    }
  }
  bool InternalSkipOptimizations_ = false;

  bool InternalBarrierOnly_ = false;
  bool InternalModuleSetupOnly_ = false;
};

/// Apply explicitly supplied hidden command-line placement and cleanup controls.
LLVM_ABI FlexFatSanitizerOptions
resolveFlexFatSanitizerOptions(FlexFatSanitizerOptions Options);

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
