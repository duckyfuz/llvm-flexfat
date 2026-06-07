; FlexFat Unit 12b: -flexfat-no-replace-alloca suppresses the entire alloca
; lowfatification path — the alloca stays native even if its address escapes.
; The Unit-10 forward-declared flag finally gets its behavioral test.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-replace-alloca -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @sink(ptr)

define void @escape(ptr %p) {
entry:
  %a = alloca i64, align 8
  store ptr %a, ptr %p, align 8
  ret void
}

; CHECK-LABEL: define void @escape(ptr %p)
; CHECK:      alloca i64, align 8
; CHECK-NOT:  flexfat.stack.mirror
; CHECK-NOT:  alloca i8, i64
