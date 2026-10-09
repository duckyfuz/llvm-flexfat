; RUN: opt < %s -passes='function(require<domtree>),flexfat,function(verify<domtree>),verify' -S | FileCheck %s

target triple = "aarch64-unknown-linux-gnu"

@value = global i8 0
@slot = global ptr null

; No bounds check is needed for either global. The parallel pointer graph and
; conditional escape block still modify the function and invalidate analyses.
define void @global_spill(i1 %take) {
; CHECK-LABEL: @global_spill(
; CHECK: [[COND:%[^ ]+]] = freeze i1 %take
; CHECK: %flexfat.escape.pointer = select i1 [[COND]], ptr null, ptr @value
; CHECK: %flexfat.escape.defined = select i1 [[COND]], i1 false, i1 true
; CHECK: br i1 %flexfat.escape.defined,
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr @slot
  %p = select i1 %take, ptr undef, ptr @value
  store ptr %p, ptr @slot
  ret void
}
