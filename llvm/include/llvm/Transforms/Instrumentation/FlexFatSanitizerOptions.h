//===- FlexFatSanitizerOptions.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFATSANITIZEROPTIONS_H
#define LLVM_TRANSFORMS_INSTRUMENTATION_FLEXFATSANITIZEROPTIONS_H

#include "llvm/ADT/StringSwitch.h"
#include <optional>

namespace llvm {

enum class FlexFatTBIMode { Off, LastByte, PriorByte, Shadow, PackedShadow };

inline std::optional<FlexFatTBIMode> parseFlexFatTBIMode(StringRef Name) {
  return StringSwitch<std::optional<FlexFatTBIMode>>(Name)
      .Case("off", FlexFatTBIMode::Off)
      .Case("last-byte", FlexFatTBIMode::LastByte)
      .Case("prior-byte", FlexFatTBIMode::PriorByte)
      .Case("shadow", FlexFatTBIMode::Shadow)
      .Case("packed-shadow", FlexFatTBIMode::PackedShadow)
      .Default(std::nullopt);
}

} // namespace llvm

#endif
