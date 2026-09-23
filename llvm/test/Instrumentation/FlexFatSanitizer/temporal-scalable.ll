; RUN: opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s --implicit-check-not="call void @__flexfat_check_temporal"
; RUN: opt -passes='flexfat<tbi>,flexfat<tbi>,verify' -S %s | FileCheck %s --implicit-check-not="call void @__flexfat_check_temporal"

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

define <vscale x 4 x i32> @load_vector(ptr %p) {
; CHECK-LABEL: define <vscale x 4 x i32> @load_vector(
; CHECK: [[VS:%.*]] = call i64 @llvm.vscale.i64(), !nosanitize
; CHECK-NEXT: [[SIZE:%.*]] = mul nuw i64 [[VS]], 16, !nosanitize
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 [[SIZE]], i32 0)
; CHECK-NEXT: %v = load <vscale x 4 x i32>, ptr %p
; CHECK-NOT: call void @__flexfat_check_temporal
; CHECK: ret <vscale x 4 x i32> %v
  %v = load <vscale x 4 x i32>, ptr %p
  ret <vscale x 4 x i32> %v
}

define void @store_vector(ptr %p, <vscale x 2 x i32> %v) {
; CHECK-LABEL: define void @store_vector(
; CHECK: [[VS:%.*]] = call i64 @llvm.vscale.i64(), !nosanitize
; CHECK-NEXT: [[SIZE:%.*]] = mul nuw i64 [[VS]], 8, !nosanitize
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 [[SIZE]], i32 1)
; CHECK-NEXT: store <vscale x 2 x i32> %v, ptr %p
; CHECK-NOT: call void @__flexfat_check_temporal
; CHECK: ret void
  store <vscale x 2 x i32> %v, ptr %p
  ret void
}

; Statically excluded pointers must not introduce unused vscale computations.
define <vscale x 4 x i32> @stack_vector(<vscale x 4 x i32> %v) {
; CHECK-LABEL: define <vscale x 4 x i32> @stack_vector(
; CHECK-NOT: call i64 @llvm.vscale
; CHECK-NOT: call void @__flexfat_check_temporal
; CHECK: ret <vscale x 4 x i32> %r
  %p = alloca <vscale x 4 x i32>
  store <vscale x 4 x i32> %v, ptr %p
  %r = load <vscale x 4 x i32>, ptr %p
  ret <vscale x 4 x i32> %r
}
