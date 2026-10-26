; RUN: opt -passes='flexfat<tbi=last-byte>,default<O2>,verify' -S %s | FileCheck %s --check-prefix=OPT
; RUN: opt -passes='flexfat<tbi=last-byte>,default<O2>,flexfat<tbi=last-byte>,verify' -S %s | FileCheck %s --check-prefix=OPT
; RUN: opt -passes='flexfat<tbi=last-byte>,verify' -S %s | FileCheck %s --check-prefixes=CHECK,LOOPS
; RUN: opt -passes='flexfat<tbi=last-byte>,flexfat<tbi=last-byte>,verify' -S %s | FileCheck %s --check-prefixes=CHECK,LOOPS

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"
declare void @may_free(ptr)
declare ptr @malloc(i64)

define i8 @invariant(ptr %p, i1 %again) {
; CHECK-LABEL: @invariant(
; CHECK: %flexfat.metadata = inttoptr
; CHECK: br label %body
; CHECK: body:
; CHECK-NOT: load i64
; CHECK: load i8, ptr %flexfat.metadata
; CHECK: %a = load volatile i8, ptr %p
; CHECK: call void @may_free
; CHECK-NOT: load i64
; CHECK: load i8, ptr %flexfat.metadata
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

define i8 @contained(i1 %again) {
; CHECK-LABEL: @contained(
; CHECK: %p = call ptr @malloc
; CHECK: %flexfat.metadata = inttoptr
; CHECK: br label %body
; CHECK: body:
; CHECK: %q = getelementptr i8, ptr %p, i64 3
; CHECK-NOT: load i64
; CHECK: load i8, ptr %flexfat.metadata
; CHECK: %a = load volatile i8, ptr %q
; CHECK: %r = getelementptr i8, ptr %p, i64 7
; CHECK-NOT: load i64
; CHECK: load i8, ptr %flexfat.metadata
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

define void @loop(ptr %p, i64 %n) {
; OPT-LABEL: define void @loop(
; OPT: br label %[[LOOP:[a-zA-Z0-9._]+]]
; OPT: [[LOOP]]:
; OPT: load i8, ptr {{.*}}, align 1
; OPT: store volatile i8 1, ptr %p
; OPT: br i1 {{.*}}, label %{{.*}}, label %{{.*}}
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

!1 = distinct !{!1, !2, !3}
!2 = !{!"llvm.loop.unroll.disable"}
!3 = !{!"llvm.loop.vectorize.enable", i1 false}

@foreign = global [64 x i8] zeroinitializer

; LOOPS-LABEL: @scan(
; LOOPS: body:
; LOOPS: %q = getelementptr i64, ptr %p, i64 %i
; LOOPS: call void @__flexfat_report_oob
; LOOPS: load i8, ptr %flexfat.metadata
; LOOPS: call void @__flexfat_report_temporal_v3
; LOOPS: %v = load i64, ptr %q
; LOOPS: br i1 %more, label %body
; LOOPS: ret
define i64 @scan(ptr %p, i64 %n) {
entry:
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %zero, label %ph
ph:
  br label %body
body:
  %i = phi i64 [0, %ph], [%next, %body]
  %sum = phi i64 [0, %ph], [%sum.next, %body]
  %q = getelementptr i64, ptr %p, i64 %i
  %v = load i64, ptr %q
  %sum.next = add i64 %sum, %v
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret i64 %sum.next
zero:
  ret i64 0
}

; LOOPS-LABEL: @negative(
; LOOPS: body:
; LOOPS: %offset = sub i64 0, %i
; LOOPS: %q = getelementptr i64, ptr %p, i64 %offset
; LOOPS: call void @__flexfat_report_oob
; LOOPS: load i8, ptr %flexfat.metadata
; LOOPS: call void @__flexfat_report_temporal_v3
; LOOPS: store i64 1, ptr %q
; LOOPS: br i1 %more, label %body
; LOOPS: ret
define void @negative(ptr %p, i64 %n) {
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %zero, label %ph
ph:
  br label %body
body:
  %i = phi i64 [0, %ph], [%next, %body]
  %offset = sub i64 0, %i
  %q = getelementptr i64, ptr %p, i64 %offset
  store i64 1, ptr %q
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret void
zero:
  ret void
}

; LOOPS-LABEL: @uncovered_loop(
; LOOPS-NOT: __flexfat_report
; LOOPS: body:
; LOOPS-NOT: __flexfat_report
; LOOPS: %q = getelementptr i8, ptr @foreign
; LOOPS-NOT: __flexfat_report
; LOOPS: %v = load i8, ptr %q
; LOOPS-NOT: __flexfat_report
; LOOPS: br i1 %more, label %body
; LOOPS: ret
define i8 @uncovered_loop(i64 %n) {
  br label %body
body:
  %i = phi i64 [0, %0], [%next, %body]
  %q = getelementptr i8, ptr @foreign, i64 %i
  %v = load i8, ptr %q
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret i8 %v
}

; LOOPS-LABEL: @atomic_loop(
; LOOPS: body:
; LOOPS: call void @__flexfat_report_oob
; LOOPS: load i8, ptr %flexfat.metadata
; LOOPS: call void @__flexfat_report_temporal_v3
; LOOPS: store atomic i64 1, ptr %p monotonic
; LOOPS: br i1 %more, label %body
; LOOPS: ret
define void @atomic_loop(ptr %p, i64 %n) {
  br label %body
body:
  %i = phi i64 [0, %0], [%next, %body]
  store atomic i64 1, ptr %p monotonic, align 8
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret void
}

; LOOPS-LABEL: @conditional(
; LOOPS: body:
; LOOPS: br i1 %enabled, label %access, label %latch
; LOOPS: access:
; LOOPS: call void @__flexfat_report_oob
; LOOPS: load i8, ptr %flexfat.metadata
; LOOPS: call void @__flexfat_report_temporal_v3
; LOOPS: store volatile i64 1, ptr %p
; LOOPS: latch:
; LOOPS: br i1 %more, label %body
; LOOPS: ret
define void @conditional(ptr %p, i64 %n, i1 %enabled) {
  br label %body
body:
  %i = phi i64 [0, %0], [%next, %latch]
  br i1 %enabled, label %access, label %latch
access:
  store volatile i64 1, ptr %p
  br label %latch
latch:
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret void
}

; LOOPS-LABEL: @two_roots(
; LOOPS: body:
; LOOPS: %q = getelementptr i64, ptr %p, i64 %i
; LOOPS: %s = getelementptr i64, ptr %r, i64 %i
; LOOPS: call void @__flexfat_report_oob
; LOOPS: load i8, ptr %flexfat.metadata
; LOOPS: call void @__flexfat_report_temporal_v3
; LOOPS: %v = load volatile i64, ptr %q
; LOOPS: call void @__flexfat_report_oob
; LOOPS: load i8, ptr %flexfat.metadata{{[0-9]+}}
; LOOPS: call void @__flexfat_report_temporal_v3
; LOOPS: store volatile i64 %v, ptr %s
; LOOPS: br i1 %more, label %body
; LOOPS: ret
define void @two_roots(ptr %p, ptr %r, i64 %n) {
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %exit, label %ph
ph:
  br label %body
body:
  %i = phi i64 [0, %ph], [%next, %body]
  %q = getelementptr i64, ptr %p, i64 %i
  %s = getelementptr i64, ptr %r, i64 %i
  %v = load volatile i64, ptr %q
  store volatile i64 %v, ptr %s
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret void
}
