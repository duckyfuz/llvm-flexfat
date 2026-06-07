; FlexFat Unit 15: ESCAPE_PTR2INT (info code 8). ptrtoint emits a
; bounds check on the source POINTER only when the resulting integer
; truly escapes — used by store/call/inttoptr/return. A ptrtoint
; whose int only feeds an icmp/branch does NOT escape (no check).
;
; This test pins the escaping case. See escape_ptr2int_ugly_gep.ll
; for the deliberate carve-out.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-elide -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @escape_via_ptr2int(ptr %p, i64 %i, ptr %sink_intptr) {
entry:
  %q = getelementptr i8, ptr %p, i64 %i
  %int = ptrtoint ptr %q to i64
  ; Store the integer into memory — that's the "escape" of the int.
  store i64 %int, ptr %sink_intptr, align 8
  ret void
}

; CHECK-LABEL: define void @escape_via_ptr2int
; CHECK: call void @lowfat_oob_error(i32 8, ptr %q, ptr %{{.*}})
