; FlexFat Unit 15: `-flexfat-no-check-escapes` (the umbrella flag
; Unit 10 forward-declared as inert) now meaningfully suppresses all
; five escape kinds at once. This is the umbrella's first behavioral
; test — the granular flags
; (`-flexfat-no-check-escape-{call,return,store,ptr2int,insert}`)
; have their own slim coverage via the per-kind tests above when
; toggled, but the umbrella's job is to kill them ALL in one knob.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-elide -flexfat-no-check-escapes -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @sink(ptr)

define ptr @all_five_escape_kinds(ptr %p, i64 %i, ptr %sink_ptr, ptr %sink_int) {
entry:
  %q = getelementptr i8, ptr %p, i64 %i
  ; ESCAPE_CALL — info 5
  call void @sink(ptr %q)
  ; ESCAPE_STORE — info 7
  store ptr %q, ptr %sink_ptr, align 8
  ; ESCAPE_PTR2INT — info 8
  %int = ptrtoint ptr %q to i64
  store i64 %int, ptr %sink_int, align 8
  ; ESCAPE_INSERT — info 9 (insertelement)
  %v0 = insertelement <2 x ptr> undef, ptr %q, i32 0
  ; ESCAPE_RETURN — info 6
  ret ptr %q
}

; No escape-kind call to lowfat_oob_error should appear (info 5, 6, 7,
; 8, 9 all suppressed by the umbrella).
; CHECK-LABEL: define ptr @all_five_escape_kinds
; CHECK-NOT: call void @lowfat_oob_error(i32 5
; CHECK-NOT: call void @lowfat_oob_error(i32 6
; CHECK-NOT: call void @lowfat_oob_error(i32 7
; CHECK-NOT: call void @lowfat_oob_error(i32 8
; CHECK-NOT: call void @lowfat_oob_error(i32 9
