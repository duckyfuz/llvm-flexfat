; FlexFat Unit 15: ESCAPE_STORE (info code 7). The VALUE of a `store`
; — when it's a pointer — escapes: its address is now in memory
; readable to anyone who can reach the destination. Note that the
; pointer being stored INTO is the WRITE-kind bounds-check target (a
; separate check); only the stored VALUE pointer is the escape.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-elide -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @escape_via_store(ptr %p, i64 %i, ptr %sink) {
entry:
  %q = getelementptr i8, ptr %p, i64 %i
  store ptr %q, ptr %sink, align 8
  ret void
}

; CHECK-LABEL: define void @escape_via_store(ptr %p, i64 %i, ptr %sink)
; CHECK: call void @lowfat_oob_error(i32 7, ptr %q, ptr %{{.*}})
