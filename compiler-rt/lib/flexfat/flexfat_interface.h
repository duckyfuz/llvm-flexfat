//===-- flexfat_interface.h - FlexFat Sanitizer Runtime Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef FLEXFAT_INTERFACE_H
#define FLEXFAT_INTERFACE_H

#include "sanitizer_common/sanitizer_internal_defs.h"

using __sanitizer::uptr;

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_init();

SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_v3();
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_prior_byte_custom_v2();
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_prior_byte_pow2_v3();
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_last_byte_custom_v1();
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_last_byte_pow2_v2();
#ifndef FLEXFAT_CUSTOM_CONFIG
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_v7();
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_tbi_abi_packed_shadow_pow2_v1();
#endif
SANITIZER_INTERFACE_ATTRIBUTE NORETURN __attribute__((cold)) void
__flexfat_report_temporal_v3(uptr tagged_ptr, uptr access_size,
                             __sanitizer::u32 operation,
                             __sanitizer::u32 observed_generation);
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_check_temporal(
    uptr tagged_ptr, uptr access_size, int operation);

SANITIZER_INTERFACE_ATTRIBUTE NORETURN void
__flexfat_report_oob(uptr ptr, uptr base, uptr bound, int is_write);

SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_base(uptr ptr);

SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_size(uptr ptr);

SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_offset(uptr ptr);

SANITIZER_INTERFACE_ATTRIBUTE uptr __flexfat_get_usable_size(uptr ptr);

SANITIZER_INTERFACE_ATTRIBUTE void *__flexfat_malloc(uptr size);
SANITIZER_INTERFACE_ATTRIBUTE void __flexfat_free(void *ptr);

}  // extern "C"

#endif  // FLEXFAT_INTERFACE_H
