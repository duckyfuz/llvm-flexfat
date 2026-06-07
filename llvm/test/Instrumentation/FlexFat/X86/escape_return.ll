; FlexFat Unit 15: ESCAPE_RETURN (info code 6). Returning a pointer
; escapes — the value is now visible to the caller and anyone they
; hand it to.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-elide -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define ptr @escape_via_return(ptr %p, i64 %i) {
entry:
  %q = getelementptr i8, ptr %p, i64 %i
  ret ptr %q
}

; CHECK-LABEL: define ptr @escape_via_return(ptr %p, i64 %i)
; CHECK: call void @lowfat_oob_error(i32 6, ptr %q, ptr %{{.*}})
