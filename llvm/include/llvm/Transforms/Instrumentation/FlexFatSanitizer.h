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

  enum class Alignment { Left, Right };
  Alignment AllocationAlignment = Alignment::Left;

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
