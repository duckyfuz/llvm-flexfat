; FlexFat Unit 17 follow-up (POW2 mirror of variant_marker_nonpow2.ll).
; Under LLVM_FLEXFAT_POW2=ON the pass references __flexfat_variant_pow2,
; which the POW2 runtime defines. Verifies the link-time-mismatch guard
; from the POW2 side.
;
; REQUIRES: flexfat-pow2
;
; RUN: opt < %s -passes=flexfat-globals -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@g = global i32 0, align 4

; CHECK-DAG: @__flexfat_variant_pow2 = external constant i8
; CHECK-DAG: @__flexfat_variant_keepalive = private constant ptr @__flexfat_variant_pow2
; CHECK-DAG: @llvm.used = appending global {{.*}}ptr @__flexfat_variant_keepalive

; CHECK-NOT: __flexfat_variant_nonpow2
