; FlexFat Unit 1: the pass is a structural no-op. Verify it loads via
; -passes=flexfat and leaves the function unchanged.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; CHECK-LABEL: @load_store(
; CHECK-NEXT:  entry:
; CHECK-NEXT:    %v = load i32, ptr %p, align 4
; CHECK-NEXT:    store i32 %v, ptr %p, align 4
; CHECK-NEXT:    ret i32 %v
define i32 @load_store(ptr %p) {
entry:
  %v = load i32, ptr %p, align 4
  store i32 %v, ptr %p, align 4
  ret i32 %v
}
