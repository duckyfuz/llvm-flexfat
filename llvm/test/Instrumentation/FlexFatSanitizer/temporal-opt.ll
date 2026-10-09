; RUN: opt -passes='flexfat<tbi>,default<O2>,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<tbi>,default<O2>,flexfat<tbi>,verify' -S %s | FileCheck %s

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"
declare void @may_free(ptr)
declare i1 @again()

define i8 @separated(ptr %p) {
; CHECK-LABEL: define i8 @separated(
; CHECK: load atomic i8, ptr {{.*}} acquire, align 1
; CHECK: load volatile i8, ptr %p
; CHECK: call void @may_free(ptr {{.*}}%p)
; CHECK: load atomic i8, ptr {{.*}} acquire, align 1
; CHECK: load volatile i8, ptr %p
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  call void @may_free(ptr %p)
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %r = add i8 %a, %b
  ret i8 %r
}

; The pointer is loop invariant, but the generation observation must remain in
; the loop. A freeing call is deliberately absent from this loop.
define void @loop(ptr %p, i64 %n) {
; CHECK-LABEL: define void @loop(
; CHECK: br label %[[LOOP:[a-zA-Z0-9._]+]]
; CHECK: [[LOOP]]:
; CHECK: load atomic i8, ptr {{.*}} acquire, align 1
; CHECK: store volatile i8 1, ptr %p
; CHECK: br i1 {{.*}}, label %{{.*}}, label %{{.*}}
  br label %body
body:
  %i = phi i64 [ 0, %0 ], [ %next, %body ]
  store volatile i8 1, ptr %p, !flexfat.instrumented !0
  %next = add i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit, !llvm.loop !1
exit:
  ret void
}
!0 = !{}
!1 = distinct !{!1, !2, !3}
!2 = !{!"llvm.loop.unroll.disable"}
!3 = !{!"llvm.loop.vectorize.enable", i1 false}
