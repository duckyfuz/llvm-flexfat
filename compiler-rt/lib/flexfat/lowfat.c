//===-- lowfat.c - FlexFat runtime (LowFat reimplementation) --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FlexFat runtime core — Unit 1 stub (no real behavior). Built with the large
// code model and BMI/BMI2/LZCNT enabled to match the reference LowFat runtime
// build; those are load-bearing for the encoding/table placement that arrives
// in later units.
//
//===----------------------------------------------------------------------===//

#include "lowfat.h"

int __flexfat_runtime_present(void) { return 0; }
