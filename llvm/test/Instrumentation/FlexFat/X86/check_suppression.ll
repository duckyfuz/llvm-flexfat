; FlexFat Unit 10: per-kind check suppression. -flexfat-no-check-reads removes
; the READ (info 0) check but keeps the WRITE (info 1); -flexfat-no-check-writes
; the reverse. Default (no flag) emits both.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s --check-prefix=BOTH
; RUN: opt < %s -passes=flexfat -flexfat-no-check-reads -S | FileCheck %s --check-prefix=NOREAD
; RUN: opt < %s -passes=flexfat -flexfat-no-check-writes -S | FileCheck %s --check-prefix=NOWRITE

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @rw(ptr %p, i64 %i) {
  %q = getelementptr i8, ptr %p, i64 %i
  %v = load i8, ptr %q
  store i8 %v, ptr %q
  ret void
}

; BOTH: call void @lowfat_oob_error(i32 0,
; BOTH: call void @lowfat_oob_error(i32 1,

; NOREAD-NOT: call void @lowfat_oob_error(i32 0,
; NOREAD:     call void @lowfat_oob_error(i32 1,

; NOWRITE:     call void @lowfat_oob_error(i32 0,
; NOWRITE-NOT: call void @lowfat_oob_error(i32 1,
