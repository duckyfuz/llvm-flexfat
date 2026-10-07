; RUN: opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s --implicit-check-not=flexfat.fast --implicit-check-not=flexfat.trip
; RUN: opt -passes='flexfat<tbi>,flexfat<tbi>,verify' -S %s | FileCheck %s --implicit-check-not=flexfat.fast --implicit-check-not=flexfat.trip

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

define i64 @scan(ptr %p, i64 %n) {
; CHECK-LABEL: @scan(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
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

define void @negative(ptr %p, i64 %n) {
; CHECK-LABEL: @negative(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
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

declare void @may_free(ptr)
define void @unsupported(ptr %p, i64 %n) {
; CHECK-LABEL: @unsupported(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
  br label %body
body:
  %i = phi i64 [0, %0], [%next, %body]
  call void @may_free(ptr %p)
  %q = getelementptr i8, ptr %p, i64 %i
  store i8 1, ptr %q
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret void
}

declare ptr @malloc(i64)
define i64 @static_containment() {
; CHECK-LABEL: @static_containment(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
  %p = call ptr @malloc(i64 64)
  br label %body
body:
  %i = phi i64 [0, %0], [%next, %body]
  %q = getelementptr i64, ptr %p, i64 %i
  %v = load i64, ptr %q
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, 8
  br i1 %more, label %body, label %exit
exit:
  ret i64 %v
}

@foreign = global [64 x i8] zeroinitializer
define i8 @uncovered_loop(i64 %n) {
; CHECK-LABEL: @uncovered_loop(
; CHECK-NOT: flexfat.metadata
; CHECK-NOT: __flexfat_report
; CHECK: ret i8
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

define i64 @volatile_scan(ptr %p, i64 %n) {
; CHECK-LABEL: @volatile_scan(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
entry:
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %zero, label %ph
ph:
  br label %body
body:
  %i = phi i64 [0, %ph], [%next, %body]
  %sum = phi i64 [0, %ph], [%sum.next, %body]
  %q = getelementptr i64, ptr %p, i64 %i
  %v = load volatile i64, ptr %q
  %sum.next = add i64 %sum, %v
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret i64 %sum.next
zero:
  ret i64 0
}


define i64 @fixed(ptr %p, i64 %n) {
; CHECK-LABEL: @fixed(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
entry:
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %zero, label %ph
ph:
  br label %body
body:
  %i = phi i64 [0, %ph], [%next, %body]
  %sum = phi i64 [0, %ph], [%sum.next, %body]
  %q = getelementptr i64, ptr %p, i64 0
  %v = load volatile i64, ptr %q
  %sum.next = add i64 %sum, %v
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret i64 %sum.next
zero:
  ret i64 0
}


define void @volatile_negative(ptr %p, i64 %n) {
; CHECK-LABEL: @volatile_negative(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
  %empty = icmp eq i64 %n, 0
  br i1 %empty, label %zero, label %ph
ph:
  br label %body
body:
  %i = phi i64 [0, %ph], [%next, %body]
  %offset = sub i64 0, %i
  %q = getelementptr i64, ptr %p, i64 %offset
  store volatile i64 1, ptr %q
  %next = add nuw i64 %i, 1
  %more = icmp ult i64 %next, %n
  br i1 %more, label %body, label %exit
exit:
  ret void
zero:
  ret void
}


define void @atomic_loop(ptr %p, i64 %n) {
; CHECK-LABEL: @atomic_loop(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
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
define void @conditional(ptr %p, i64 %n, i1 %enabled) {
; CHECK-LABEL: @conditional(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
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

define void @two_roots(ptr %p, ptr %r, i64 %n) {
; CHECK-LABEL: @two_roots(
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3
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
