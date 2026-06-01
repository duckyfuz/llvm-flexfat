//===-- lowfat.h - FlexFat runtime (LowFat reimplementation) --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// FlexFat runtime — Unit 1 stub. The runtime keeps the LowFat ABI symbol names
// (lowfat_*) byte-identical to the reference so the verification harness can
// diff against it. None of that ABI exists yet: this header only declares a
// presence sentinel so the stub translation unit and its tests have something
// to reference. The real surface (lowfat_base, lowfat_oob_check, the heap
// allocator, and the fixed tables at 0x200000/0x300000) lands in later units.
//
//===----------------------------------------------------------------------===//
#ifndef FLEXFAT_LOWFAT_H
#define FLEXFAT_LOWFAT_H

#ifdef __cplusplus
extern "C" {
#endif

// Returns 0. Exists only so the Unit 1 stub is a non-empty translation unit.
int __flexfat_runtime_present(void);

#ifdef __cplusplus
}
#endif

#endif // FLEXFAT_LOWFAT_H
