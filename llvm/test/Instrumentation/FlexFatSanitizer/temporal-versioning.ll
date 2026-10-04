; RUN: opt -passes='function(loop-simplify,lcssa),flexfat<tbi>,verify' -S %s | FileCheck %s
; RUN: opt -passes='function(loop-simplify,lcssa),flexfat<tbi>,flexfat<tbi>,verify' -S %s | FileCheck %s

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

define i64 @scan(ptr %p, i64 %n) {
; CHECK-LABEL: @scan(
; CHECK-NOT: load atomic i8
; CHECK: %empty = icmp eq i64 %n, 0
; CHECK: br i1 %empty, label %zero, label %ph
; CHECK: ph:
; CHECK: call { i64, i1 } @llvm.smul.with.overflow.i64
; CHECK: call { i64, i1 } @llvm.sadd.with.overflow.i64
; CHECK: icmp ule i64
; CHECK: br i1 {{.*}}, label %flexfat.fallback.ph.flexfat.fast, label %flexfat.fallback.ph
; CHECK: body.flexfat.fast:
; CHECK-NOT: load i64, ptr {{.*}}!invariant.load
; CHECK: load atomic i8, ptr %flexfat.metadata{{[0-9]*}} acquire
; CHECK: %v.flexfat.fast = load i64, ptr %q.flexfat.fast
; CHECK-NOT: __flexfat_report_oob
; CHECK: flexfat.fallback.ph:
; CHECK: body:
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: %v = load i64, ptr %q
; CHECK: exit:
; CHECK: phi i64 {{.*}}%sum.next.flexfat.fast
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
; CHECK: call { i64, i1 } @llvm.smul.with.overflow.i64(i64 {{.*}}, i64 -8)
; CHECK: body.flexfat.fast:
; CHECK: load atomic i8
; CHECK: store i64 1
; CHECK: body:
; CHECK: load atomic i8
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
; CHECK-NOT: flexfat.fast
; CHECK: call void @may_free
; CHECK: load atomic i8
; CHECK: store i8 1
; CHECK-NOT: flexfat.fast
; CHECK: ret void
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
; CHECK: %p = call ptr @malloc(i64 64)
; CHECK: %flexfat.metadata = inttoptr
; CHECK-NOT: with.overflow
; CHECK-NOT: flexfat.fast
; CHECK: body:
; CHECK: load atomic i8, ptr %flexfat.metadata acquire
; CHECK: = or i1
; CHECK-NOT: __flexfat_report_oob
; CHECK: %v = load i64, ptr %q
; CHECK-NOT: flexfat.fast
; CHECK: ret i64
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
; CHECK-NOT: flexfat.fast
; CHECK-NOT: load atomic
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
; CHECK-NOT: load atomic i8
; CHECK: %empty = icmp eq i64 %n, 0
; CHECK: br i1 %empty, label %zero, label %ph
; CHECK: ph:
; CHECK: call { i64, i1 } @llvm.smul.with.overflow.i64
; CHECK: call { i64, i1 } @llvm.sadd.with.overflow.i64
; CHECK: icmp ule i64
; CHECK: br i1 {{.*}}, label %flexfat.fallback.ph.flexfat.fast, label %flexfat.fallback.ph
; CHECK: body.flexfat.fast:
; CHECK-NOT: load i64, ptr {{.*}}!invariant.load
; CHECK: load atomic i8, ptr %flexfat.metadata{{[0-9]*}} acquire
; CHECK-NOT: load atomic i8
; CHECK-NOT: = or i1
; CHECK: %v.flexfat.fast = load volatile i64, ptr %q.flexfat.fast
; CHECK-NOT: __flexfat_report_oob
; CHECK: flexfat.fallback.ph:
; CHECK: body:
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: %v = load volatile i64, ptr %q
; CHECK: exit:
; CHECK: phi i64 {{.*}}%sum.next.flexfat.fast
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
; CHECK-NOT: load atomic i8
; CHECK: %empty = icmp eq i64 %n, 0
; CHECK: br i1 %empty, label %zero, label %ph
; CHECK: ph:
; CHECK: br i1 {{.*}}, label %flexfat.fallback.ph.flexfat.fast, label %flexfat.fallback.ph
; CHECK: body.flexfat.fast:
; CHECK-NOT: load i64, ptr {{.*}}!invariant.load
; CHECK: load atomic i8, ptr %flexfat.metadata{{[0-9]*}} acquire
; CHECK-NOT: load atomic i8
; CHECK-NOT: = or i1
; CHECK: %v.flexfat.fast = load volatile i64, ptr %q.flexfat.fast
; CHECK-NOT: __flexfat_report_oob
; CHECK: flexfat.fallback.ph:
; CHECK: body:
; CHECK: call void @__flexfat_report_oob
; CHECK: load atomic i8
; CHECK: %v = load volatile i64, ptr %q
; CHECK: exit:
; CHECK: phi i64 {{.*}}%sum.next.flexfat.fast
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
; CHECK: call { i64, i1 } @llvm.smul.with.overflow.i64(i64 {{.*}}, i64 -8)
; CHECK: body.flexfat.fast:
; CHECK: load atomic i8
; CHECK: store volatile i64 1
; CHECK: body:
; CHECK: load atomic i8
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
; CHECK-NOT: flexfat.fast
; CHECK: store atomic i64 1
; CHECK-NOT: flexfat.fast
; CHECK: ret void
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
; CHECK-NOT: flexfat.fast
; CHECK: store volatile i64 1
; CHECK-NOT: flexfat.fast
; CHECK: ret void
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
; CHECK: %flexfat.metadata = inttoptr
; CHECK: %flexfat.metadata{{[0-9]+}} = inttoptr
; CHECK: body.flexfat.fast:
; CHECK: ptrtoint ptr %q.flexfat.fast to i64
; CHECK: load atomic i8, ptr %flexfat.metadata{{[0-9]*}} acquire
; CHECK-NOT: load atomic i8
; CHECK-NOT: = or i1
; CHECK: load volatile i64, ptr %q.flexfat.fast
; CHECK: ptrtoint ptr %s.flexfat.fast to i64
; CHECK: load atomic i8, ptr %flexfat.metadata{{[0-9]*}} acquire
; CHECK-NOT: load atomic i8
; CHECK-NOT: = or i1
; CHECK: store volatile i64 %v.flexfat.fast, ptr %s.flexfat.fast
; CHECK-NOT: __flexfat_report_oob
; CHECK: flexfat.fallback.ph:
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
