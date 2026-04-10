//===-- lf_interface.h - LowFat Sanitizer Runtime Interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Public interface functions implemented by the LowFat runtime. The LLVM
// instrumentation pass emits calls to __lf_report_oob / __lf_warn_oob;
// the remaining functions are available for programmatic use.
//
//===----------------------------------------------------------------------===//

#ifndef LF_INTERFACE_H
#define LF_INTERFACE_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;

extern "C" {

// Initialize the runtime. Called via .preinit_array or constructor.
SANITIZER_INTERFACE_ATTRIBUTE void __lf_init();

// Configure recover mode (warn instead of abort on OOB).
SANITIZER_INTERFACE_ATTRIBUTE void __lf_set_recover(int recover);

// Configure right-align mode (bias allocations toward slot end).
SANITIZER_INTERFACE_ATTRIBUTE void __lf_set_right_align(int right_align);

// Fatal OOB report — called from instrumented code.
SANITIZER_INTERFACE_ATTRIBUTE void __lf_report_oob(uptr ptr, uptr base,
                                                    uptr bound, int is_write);

// Non-fatal OOB warning — called in recover mode.
SANITIZER_INTERFACE_ATTRIBUTE void __lf_warn_oob(uptr ptr, uptr base,
                                                  uptr bound, int is_write);

// Query functions for programmatic use.
SANITIZER_INTERFACE_ATTRIBUTE uptr __lf_get_base(uptr ptr);
SANITIZER_INTERFACE_ATTRIBUTE uptr __lf_get_size(uptr ptr);
SANITIZER_INTERFACE_ATTRIBUTE uptr __lf_get_offset(uptr ptr);
SANITIZER_INTERFACE_ATTRIBUTE uptr __lf_get_usable_size(uptr ptr);

// Direct allocator access.
SANITIZER_INTERFACE_ATTRIBUTE void *__lf_malloc(uptr size);
SANITIZER_INTERFACE_ATTRIBUTE void __lf_free(void *ptr);

}  // extern "C"

#endif  // LF_INTERFACE_H
