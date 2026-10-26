; RUN: opt -passes='flexfat,function(early-cse<memssa>,instcombine,simplifycfg),flexfat,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<tbi=last-byte>,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s --check-prefix=TEMPORAL
; RUN: opt -passes='flexfat<tbi=prior-byte>,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s --check-prefix=TEMPORAL
; RUN: opt -passes='flexfat<tbi=last-byte>,function(early-cse<memssa>,instcombine,simplifycfg),flexfat<tbi=last-byte>,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s --check-prefix=TEMPORAL

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

; CHECK-LABEL: define i32 @read_value(
; CHECK: call void @__flexfat_report_oob
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: ret i32
define i32 @read_value(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; TEMPORAL: @llvm.global_ctors = appending global [1 x
; TEMPORAL-LABEL: define i8 @twice(
; TEMPORAL: load i8
; TEMPORAL: %a = load volatile i8, ptr %p
; TEMPORAL-NOT: load i8
; TEMPORAL: %b = load volatile i8, ptr %p
; TEMPORAL-NOT: load i8
; TEMPORAL: ret i8
define i8 @twice(ptr %p) {
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %sum = add i8 %a, %b
  ret i8 %sum
}
!0 = !{}
