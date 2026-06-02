; FlexFat leaves accesses to non-fat pointers uninstrumented. Here the base is a
; stack alloca (stack lowfatification is a later unit) and a global -- both
; non-fat in the LOAD/STORE unit -- so calcBasePtr yields a NULL base and the
; check is dropped: the function is left unchanged.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@g = global i32 0

define i32 @nonfat() {
entry:
  %a = alloca i32, align 4
  store i32 42, ptr %a, align 4
  %v = load i32, ptr %a, align 4
  %w = load i32, ptr @g, align 4
  %s = add i32 %v, %w
  ret i32 %s
}

; CHECK-LABEL: define i32 @nonfat()
; CHECK-NEXT:  entry:
; CHECK-NEXT:    %a = alloca i32, align 4
; CHECK-NEXT:    store i32 42, ptr %a, align 4
; CHECK-NEXT:    %v = load i32, ptr %a, align 4
; CHECK-NEXT:    %w = load i32, ptr @g, align 4
; CHECK-NEXT:    %s = add i32 %v, %w
; CHECK-NEXT:    ret i32 %s
; CHECK-NOT:   lowfat_oob_error
