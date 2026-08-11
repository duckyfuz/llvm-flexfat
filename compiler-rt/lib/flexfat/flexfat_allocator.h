//===-- flexfat_allocator.h - FlexFat Allocator Internal Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Internal allocator interface shared between flexfat_rtl.cpp and
// flexfat_interceptors.cpp.
//
//===----------------------------------------------------------------------===//
#ifndef LF_ALLOCATOR_H
#define LF_ALLOCATOR_H

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __flexfat {

using __sanitizer::uptr;

// Allocate from a FlexFat region. Returns nullptr if size exceeds max.
void *Allocate(uptr size);

// Free a FlexFat allocation.
void Deallocate(void *ptr);

// Initialize interceptors (called from __flexfat_init).
void InitializeInterceptors();

} // namespace __flexfat

#endif // LF_ALLOCATOR_H
