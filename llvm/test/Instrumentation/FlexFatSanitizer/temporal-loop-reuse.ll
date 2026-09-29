; RUN: opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<tbi>,flexfat<tbi>,verify' -S %s | FileCheck %s

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"
declare void @may_free(ptr)
declare ptr @malloc(i64)

; Even spatially marked accesses share immutable loop-invariant geometry.
; A call between accesses must not allow either acquire observation to move.
define i8 @invariant(ptr %p, i1 %again) {
; CHECK-LABEL: @invariant(
; CHECK: %flexfat.metadata = inttoptr
; CHECK: br label %body
; CHECK: body:
; CHECK-NOT: load i64
; CHECK: load atomic i8, ptr %flexfat.metadata acquire
; CHECK: %a = load volatile i8, ptr %p
; CHECK: call void @may_free
; CHECK-NOT: load i64
; CHECK: load atomic i8, ptr %flexfat.metadata acquire
; CHECK: %b = load volatile i8, ptr %p
; CHECK: ret i8
  br label %body
body:
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  call void @may_free(ptr %p), !flexfat.instrumented !0
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  br i1 %again, label %body, label %exit
exit:
  ret i8 %b
}

; Constant offsets into a known allocation are in the same slot even when
; the GEP instructions occur inside a loop. Geometry is shared at its root.
define i8 @contained(i1 %again) {
; CHECK-LABEL: @contained(
; CHECK: %p = call ptr @malloc
; CHECK: %flexfat.metadata = inttoptr
; CHECK: br label %body
; CHECK: body:
; CHECK: %q = getelementptr i8, ptr %p, i64 3
; CHECK-NOT: load i64
; CHECK: load atomic i8, ptr %flexfat.metadata acquire
; CHECK: %a = load volatile i8, ptr %q
; CHECK: %r = getelementptr i8, ptr %p, i64 7
; CHECK-NOT: load i64
; CHECK: load atomic i8, ptr %flexfat.metadata acquire
; CHECK: %b = load volatile i8, ptr %r
  %p = call ptr @malloc(i64 16)
  br label %body
body:
  %q = getelementptr i8, ptr %p, i64 3
  %a = load volatile i8, ptr %q, !flexfat.instrumented !0
  %r = getelementptr i8, ptr %p, i64 7
  %b = load volatile i8, ptr %r, !flexfat.instrumented !0
  br i1 %again, label %body, label %exit
exit:
  ret i8 %b
}
!0 = !{}
