; FlexFat Unit 15: ESCAPE_CALL (info code 5 per lowfat.h:45). A pointer
; argument passed to a call/invoke escapes — `lowfat_oob_error` is
; called with `i32 5` in the cold error block. -flexfat-no-elide
; disables Unit 8's static bounds elision so the check is visible
; regardless of inferred bounds.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-elide -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @sink(ptr)

define void @escape_via_call(ptr %p) {
entry:
  ; A second-level GEP off the input makes the static bounds Unknown,
  ; so the escape check is emitted rather than elided.
  %q = getelementptr i8, ptr %p, i64 1
  call void @sink(ptr %q)
  ret void
}

; CHECK-LABEL: define void @escape_via_call(ptr %p)
; CHECK: call void @lowfat_oob_error(i32 5, ptr %q, ptr %{{.*}})
