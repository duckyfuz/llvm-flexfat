; RUN: opt < %s -passes='flexfat,verify' -S | FileCheck %s
; RUN: opt < %s -passes='flexfat<whole-access>,verify' -S | FileCheck %s
; RUN: opt < %s -passes='flexfat,default<O2>,verify' -disable-output
; RUN: opt < %s -passes='default<O2>,flexfat,verify' -disable-output
; RUN: opt < %s -passes='flexfat,verify' -S -o %t.once
; RUN: opt < %s -passes='flexfat,flexfat,verify' -S -o %t.twice
; RUN: diff %t.once %t.twice

target triple = "aarch64-unknown-linux-gnu"

@value = global i8 0
@slot = global ptr null

; Storing poison is legal. Even an elided escape check must not introduce a
; poison branch. Keep the original select and store intact.
define void @poison_global() {
; CHECK-LABEL: @poison_global(
; CHECK: %p = select i1 poison, ptr undef, ptr @value
; CHECK: [[COND:%[^ ]+]] = freeze i1 poison
; CHECK: select i1 [[COND]], ptr null, ptr @value
; CHECK: [[DEFINED:%[^ ]+]] = select i1 [[COND]], i1 false, i1 true
; CHECK: br i1 [[DEFINED]],
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr @slot
  %p = select i1 poison, ptr undef, ptr @value
  store ptr %p, ptr @slot
  ret void
}

; Freezing only the final branch predicate would still allow poison to reach
; speculative metadata loads. Both check-only selects need the same condition.
define void @poison_dynamic(ptr %input) {
; CHECK-LABEL: @poison_dynamic(
; CHECK: %p = select i1 poison, ptr undef, ptr %q
; CHECK: [[COND:%[^ ]+]] = freeze i1 poison
; CHECK: [[SAFE:%[^ ]+]] = select i1 [[COND]], ptr null, ptr %q
; CHECK: ptrtoint ptr [[SAFE]] to i64
; CHECK: [[DEFINED:%[^ ]+]] = select i1 [[COND]], i1 false, i1 true
; CHECK: br i1 [[DEFINED]],
; CHECK: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr @slot
  %q = getelementptr i8, ptr %input, i64 1
  %p = select i1 poison, ptr undef, ptr %q
  store ptr %p, ptr @slot
  ret void
}

; Separate uses of undef can choose different arms without a shared freeze.
define void @undef_dynamic(ptr %input) {
; CHECK-LABEL: @undef_dynamic(
; CHECK: %p = select i1 undef, ptr poison, ptr %q
; CHECK: [[COND:%[^ ]+]] = freeze i1 undef
; CHECK: [[SAFE:%[^ ]+]] = select i1 [[COND]], ptr null, ptr %q
; CHECK: ptrtoint ptr [[SAFE]] to i64
; CHECK: [[DEFINED:%[^ ]+]] = select i1 [[COND]], i1 false, i1 true
; CHECK: br i1 [[DEFINED]],
; CHECK: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr @slot
  %q = getelementptr i8, ptr %input, i64 1
  %p = select i1 undef, ptr poison, ptr %q
  store ptr %p, ptr @slot
  ret void
}
