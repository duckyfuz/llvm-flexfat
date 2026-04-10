//===-- lf_stack.h - LowFat stack trace utilities ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LF_STACK_H
#define LF_STACK_H

#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

#define GET_STACK_TRACE_FATAL_HERE                                             \
  UNINITIALIZED __sanitizer::BufferedStackTrace stack;                         \
  stack.Unwind(__sanitizer::StackTrace::GetCurrentPc(), GET_CURRENT_FRAME(),   \
               nullptr, common_flags()->fast_unwind_on_fatal)

#define PRINT_CURRENT_STACK()   \
  {                             \
    GET_STACK_TRACE_FATAL_HERE; \
    stack.Print();              \
  }

#endif  // LF_STACK_H
