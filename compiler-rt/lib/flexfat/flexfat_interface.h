//===-- flexfat_interface.h - FlexFat Sanitizer Runtime Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the FlexFat Sanitizer runtime interface functions.
// The runtime library must define these functions so the instrumented program
// can call them.
//
//===----------------------------------------------------------------------===//
#ifndef FLEXFAT_INTERFACE_H
#define FLEXFAT_INTERFACE_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;

extern "C" {

// Initialize the FlexFat Sanitizer runtime. Called early during program startup.
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_init();

// Report an out-of-bounds error.
// ptr: The pointer that caused the violation
// base: The base address of the allocation
// bound: The size of the allocation
// Report a fatal out-of-bounds access and terminate.
// ptr: the offending pointer, base: base of the allocation,
// bound: size of the allocation, is_write: 1=write 0=read

// Called from a compiler-generated module constructor to communicate
// -fsanitize-recover=flexfat to the runtime interceptors.
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_set_recover(int recover);

// Called from a compiler-generated module constructor when
// -flexfat-mode=right-align is active. Instructs the allocator to bias objects
// toward the high end of their size-class slot while preserving the default
// malloc alignment. This can improve detection of some small rightward
// overflows, but the alignment constraint means the object will not always end
// exactly at the slot boundary. The trade-off is a possible blind spot on the
// left side when the shifted pointer still remains within the same slot.
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_set_right_align(int right_align);

SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_report_oob(uptr ptr, uptr base,
                                                    uptr bound, int is_write);

// Warn about an out-of-bounds error without terminating.
// ptr: the offending pointer, base: base of the allocation,
// bound: size of the allocation, is_write: 1=write 0=read
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_warn_oob(uptr ptr, uptr base,
                                                  uptr bound, int is_write);

// Get the base address of an allocation from a pointer.
// Returns the base address, or 0 if the pointer is not within a FlexFat region.
SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_base(uptr ptr);

// Get the size (bound) of an allocation from a pointer.
// Returns the allocation size, or (uptr)-1 if the pointer is not within a FlexFat region.
SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_size(uptr ptr);

// Get the offset from the base address of an allocation.
// Returns the offset, or 0 if the pointer is not within a FlexFat region.
SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_offset(uptr ptr);

// Get the remaining usable size in the allocation from a pointer.
// Returns (size - offset), or (uptr)-1 if the pointer is not within a FlexFat region.
SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_usable_size(uptr ptr);

// Allocate/Deallocate from FlexFat regions.
SANITIZER_INTERFACE_ATTRIBUTE void *__flexfat_malloc(uptr size);
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_free(void *ptr);


}  // extern "C"

#endif  // FLEXFAT_INTERFACE_H
