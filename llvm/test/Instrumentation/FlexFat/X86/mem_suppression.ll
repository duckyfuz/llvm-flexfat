; FlexFat Unit 10: mem-intrinsic check suppression. -flexfat-no-check-memcpy
; removes the memcpy/memmove end-pointer checks (info 2) but keeps memset (info
; 3); -flexfat-no-check-memset the reverse.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s --check-prefix=ON
; RUN: opt < %s -passes=flexfat -flexfat-no-check-memcpy -S | FileCheck %s --check-prefix=NOCPY
; RUN: opt < %s -passes=flexfat -flexfat-no-check-memset -S | FileCheck %s --check-prefix=NOSET

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1)

define void @f(ptr %d, ptr %s, i64 %n) {
  call void @llvm.memcpy.p0.p0.i64(ptr %d, ptr %s, i64 %n, i1 false)
  call void @llvm.memset.p0.i64(ptr %d, i8 0, i64 %n, i1 false)
  ret void
}

; ON: call void @lowfat_oob_error(i32 2,
; ON: call void @lowfat_oob_error(i32 3,

; NOCPY-NOT: call void @lowfat_oob_error(i32 2,
; NOCPY:     call void @lowfat_oob_error(i32 3,

; NOSET:     call void @lowfat_oob_error(i32 2,
; NOSET-NOT: call void @lowfat_oob_error(i32 3,
