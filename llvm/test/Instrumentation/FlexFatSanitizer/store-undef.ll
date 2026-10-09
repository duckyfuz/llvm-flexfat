; RUN: opt < %s -passes='flexfat,verify' -S | FileCheck %s
; RUN: opt < %s -passes='flexfat<whole-access>,verify' -S | FileCheck %s
; RUN: opt < %s -passes='flexfat,default<O2>,verify' -disable-output
; RUN: opt < %s -passes='default<O2>,flexfat,verify' -disable-output

target triple = "aarch64-unknown-linux-gnu"

; Optimizer-created temporary spills may carry undef through loop PHIs.
; Only the sanitizer's check operands are sanitized; the original store stays.
define void @loop_spill(ptr %slot, ptr %input, i1 %update, i32 %count) {
; CHECK-LABEL: @loop_spill(
; CHECK: phi i1 [ false, %entry ],
; CHECK: phi ptr [ null, %entry ],
; CHECK: br i1 %flexfat.escape.defined, label {{.*}}, label {{.*}}
; CHECK: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr %slot
entry:
  %defined = getelementptr i8, ptr %input, i64 1
  br label %loop
loop:
  %p = phi ptr [ undef, %entry ], [ %next, %loop ]
  %n = phi i32 [ 0, %entry ], [ %inc, %loop ]
  store ptr %p, ptr %slot
  %next = select i1 %update, ptr %defined, ptr %p
  %inc = add i32 %n, 1
  %again = icmp ult i32 %inc, %count
  br i1 %again, label %loop, label %exit
exit:
  ret void
}

define void @poison_select(ptr %slot, ptr %input, i1 %take) {
; CHECK-LABEL: @poison_select(
; CHECK: [[COND:%[^ ]+]] = freeze i1 %take
; CHECK: select i1 [[COND]], ptr null, ptr %q
; CHECK: select i1 [[COND]], i1 false, i1 true
; CHECK: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr %slot
  %q = getelementptr i8, ptr %input, i64 1
  %p = select i1 %take, ptr poison, ptr %q
  store ptr %p, ptr %slot
  ret void
}

; Undefined stored values do not excuse an invalid destination.
define void @bad_destination(ptr %input) {
; CHECK-LABEL: @bad_destination(
; CHECK: call void @__flexfat_report_oob
; CHECK: store ptr undef, ptr %dest
  %dest = getelementptr i8, ptr %input, i64 16
  store ptr undef, ptr %dest
  ret void
}

; A derived check-only pointer must not turn the substituted null into poison
; through an inbounds promise. Defined paths retain the original provenance.
define void @derived_spill(ptr %slot, ptr %input, i1 %take, i64 %offset) {
; CHECK-LABEL: @derived_spill(
; CHECK: [[SOURCE_BASE:%flexfat.base[^ ]*]] = phi ptr [ null, %undefined ], [ {{%[^ ]+}}, %defined ]
; CHECK: %flexfat.escape.derived = getelementptr i8, ptr %flexfat.escape.pointer, i64 %offset
; CHECK: [[SAFE:%[^ ]+]] = select i1 %flexfat.escape.defined, ptr %flexfat.escape.derived, ptr null
; CHECK-NOT: ptrtoint ptr [[SAFE]]
; CHECK: br i1 %flexfat.escape.defined,
; CHECK: [[PTR_INT:%[^ ]+]] = ptrtoint ptr [[SAFE]] to i64
; CHECK-NEXT: [[BASE_INT:%[^ ]+]] = ptrtoint ptr [[SOURCE_BASE]] to i64
; CHECK-NEXT: [[DIFF:%[^ ]+]] = sub i64 [[PTR_INT]], [[BASE_INT]]
; CHECK-NEXT: icmp uge i64 [[DIFF]],
; CHECK: call void @__flexfat_report_oob(i64 [[PTR_INT]], i64 [[BASE_INT]],
; CHECK: store ptr %q, ptr %slot
entry:
  br i1 %take, label %undefined, label %defined
undefined:
  br label %join
defined:
  br label %join
join:
  %p = phi ptr [ undef, %undefined ], [ %input, %defined ]
  %q = getelementptr inbounds i8, ptr %p, i64 %offset
  store ptr %q, ptr %slot
  ret void
}

; Freeze produces a defined arbitrary value: it must still be checked.
define void @frozen(ptr %slot, ptr %input, i1 %take) {
; CHECK-LABEL: @frozen(
; CHECK: ptrtoint ptr %p to i64
; CHECK: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr %slot
  %q = getelementptr i8, ptr %input, i64 1
  %maybe = select i1 %take, ptr undef, ptr %q
  %p = freeze ptr %maybe
  store ptr %p, ptr %slot
  ret void
}
