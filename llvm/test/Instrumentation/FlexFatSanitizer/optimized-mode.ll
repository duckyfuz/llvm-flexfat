; RUN: opt -flexfat-post-cleanup=early-cse -flexfat-post-cleanup=none -passes='flexfat<mode=optimized>,verify' -disable-output -debug-pass-manager %s 2>&1 | FileCheck %s --check-prefix=OVERRIDE
; RUN: not opt -passes='flexfat<post-cleanup=gvn>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<loop-profitability=cost>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<temporal-hoist>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<temporal-reuse>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<extended-geometry-reuse>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: opt -passes='flexfat<post-cleanup=none;post-cleanup=early-cse;mode=optimized;post-cleanup=none>,verify' -disable-output -debug-pass-manager %s 2>&1 | FileCheck %s --check-prefix=OVERRIDE
; RUN: opt -passes='flexfat<post-cleanup=none;mode=optimized>,verify' -disable-output -debug-pass-manager %s 2>&1 | FileCheck %s --check-prefix=OVERRIDE
; RUN: opt -passes='flexfat<mode=optimized>,verify' -S %s -o %t.preset
; RUN: opt -passes='flexfat<post-cleanup=early-cse;instrumentation-point=optimizer-last>,verify' -S %s -o %t.explicit
; RUN: diff %t.preset %t.explicit
; RUN: opt -passes='flexfat<mode=optimized>,flexfat<mode=optimized>,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<post-cleanup=none;mode=optimized>,verify' -disable-output -debug-pass-manager %s 2>&1 | FileCheck %s --check-prefix=OVERRIDE
; RUN: opt -passes='flexfat<mode=optimized;mode=fast>,verify' -disable-output -debug-pass-manager %s 2>&1 | FileCheck %s --check-prefix=OVERRIDE
; RUN: not opt -passes='flexfat<mode=unknown>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=INVALID
; RUN: opt -passes='flexfat<mode=optimized;tbi;tbi-storage=last-byte>,verify' -S %s | FileCheck %s --check-prefix=ATOMIC
; RUN: opt -passes='flexfat<mode=optimized;tbi;tbi-storage=prior-byte>,verify' -S %s | FileCheck %s --check-prefix=ATOMIC
; RUN: opt -passes='flexfat<mode=optimized;tbi>,flexfat<mode=optimized;tbi>,verify' -S %s | FileCheck %s --check-prefix=ATOMIC

; OVERRIDE: Running pass: FlexFatSanitizerPass
; OVERRIDE-NOT: Running pass: EarlyCSEPass
; INVALID: invalid FlexFatSanitizer pass parameter 'mode=unknown'

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

; REMOVED: invalid FlexFatSanitizer pass parameter
