; The standalone instrumentation pass composes with the cleanup pipeline
; installed by Clang. Repeated instrumentation must remain idempotent.
; RUN: opt -passes='flexfat,function(early-cse<memssa>,instcombine,simplifycfg),flexfat,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<tbi;tbi-storage=last-byte>,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s --check-prefix=ATOMIC
; RUN: opt -passes='flexfat<tbi;tbi-storage=prior-byte>,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s --check-prefix=ATOMIC
; RUN: opt -passes='flexfat<tbi>,function(early-cse<memssa>,instcombine,simplifycfg),flexfat<tbi>,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s --check-prefix=ATOMIC

; RUN: not opt -passes='flexfat<mode=fast>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<mode=safe>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<mode=optimized>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<instrumentation-point=optimizer-last>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<post-cleanup=early-cse>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; REMOVED: invalid FlexFatSanitizer pass parameter

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

; Repeated runs retain one spatial check for the access.
; CHECK-LABEL: define i32 @read_value(
; CHECK: call void @__flexfat_report_oob
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: ret i32
define i32 @read_value(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; Cleanup must preserve each atomic temporal observation, including when
; there is no intervening invalidation.
; ATOMIC: @llvm.global_ctors = appending global [1 x
; ATOMIC-LABEL: define i8 @twice(
; ATOMIC: load atomic i8
; ATOMIC: %a = load volatile i8, ptr %p
; ATOMIC: load atomic i8
; ATOMIC: %b = load volatile i8, ptr %p
define i8 @twice(ptr %p) {
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %sum = add i8 %a, %b
  ret i8 %sum
}
!0 = !{}
