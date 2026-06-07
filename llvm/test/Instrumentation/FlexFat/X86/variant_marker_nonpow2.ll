; FlexFat Unit 17 follow-up: link-time variant assertion. The Globals pass
; emits a private constant pointing at __flexfat_variant_{pow2,nonpow2}
; (chosen by the pass's compile-time FLEXFAT_IS_POW2) and pins it to
; llvm.used. The runtime defines exactly one of the two symbols; a mixed
; build (pass POW2 + runtime non-POW2 or vice versa) fails to LINK on the
; undefined external. This test pins the IR shape for the non-POW2 build.
;
; REQUIRES: flexfat-nonpow2
;
; RUN: opt < %s -passes=flexfat-globals -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@g = global i32 0, align 4

; CHECK-DAG: @__flexfat_variant_nonpow2 = external constant i8
; CHECK-DAG: @__flexfat_variant_keepalive = private constant ptr @__flexfat_variant_nonpow2
; CHECK-DAG: @llvm.used = appending global {{.*}}ptr @__flexfat_variant_keepalive

; And the pass-POW2 symbol must NOT appear:
; CHECK-NOT: __flexfat_variant_pow2
