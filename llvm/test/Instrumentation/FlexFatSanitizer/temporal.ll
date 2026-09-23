; RUN: opt -passes='flexfat<tbi>,flexfat<tbi>,verify' -S %s | FileCheck %s
; RUN: not --crash opt -mtriple=x86_64-linux-gnu -passes='flexfat<tbi>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=BAD
; RUN: not --crash opt -mtriple=aarch64-unknown-linux-gnu_ilp32 -passes='flexfat<tbi>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=BAD
; RUN: sed 's/e-p:64:64/E-p:64:64/' %s | not --crash opt -passes='flexfat<tbi>' -disable-output 2>&1 | FileCheck %s --check-prefix=BAD
; RUN: sed 's/e-p:64:64/e-p:32:32/' %s | not --crash opt -passes='flexfat<tbi>' -disable-output 2>&1 | FileCheck %s --check-prefix=BAD
; BAD: FlexFat TBI requires little-endian Linux AArch64 with 64-bit pointers

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"
; CHECK: @llvm.global_ctors =
; CHECK: @llvm.used =

declare void @__flexfat_check_temporal(i64, i64, i32) memory(none) speculatable
declare ptr @malloc(i64)
declare void @may_free(ptr)
declare void @llvm.memcpy.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memmove.p0.p0.i64(ptr, ptr, i64, i1 immarg)
declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)

; Spatial elimination at the allocation root must not remove temporal checks.
define i32 @allocated() {
; CHECK-LABEL: define i32 @allocated
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 4, i32 1)
; CHECK-NEXT: store i32 7, ptr %p
; CHECK: call void @may_free
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 4, i32 0)
; CHECK-NEXT: %v = load i32, ptr %p
; CHECK-NOT: call void @__flexfat_check_temporal
  %p = call ptr @malloc(i64 16)
  store i32 7, ptr %p
  call void @may_free(ptr %p)
  %v = load i32, ptr %p
  ret i32 %v
}

define void @coverage(ptr %p, ptr %q, i64 %n) {
; CHECK-LABEL: define void @coverage
; CHECK: and i64 {{.*}}, 72057594037927935
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 16, i32 0)
; CHECK: load <4 x i32>, ptr %p
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 4, i32 1)
; CHECK: atomicrmw
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 4, i32 1)
; CHECK: cmpxchg
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 %n, i32 1)
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 %n, i32 0)
; CHECK: call void @llvm.memcpy
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 %n, i32 1)
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 %n, i32 0)
; CHECK: call void @llvm.memmove
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 0, i32 1)
; CHECK: call void @llvm.memset
  %v = load <4 x i32>, ptr %p
  %a = atomicrmw add ptr %p, i32 1 seq_cst
  %b = cmpxchg ptr %p, i32 1, i32 2 seq_cst seq_cst
  call void @llvm.memcpy.p0.p0.i64(ptr %p, ptr %q, i64 %n, i1 false)
  call void @llvm.memmove.p0.p0.i64(ptr %p, ptr %q, i64 %n, i1 false)
  call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 0, i1 false)
  ret void
}

define void @loop(ptr %p, i1 %again) {
; CHECK-LABEL: define void @loop
; CHECK: body:
; CHECK: call void @__flexfat_check_temporal
; CHECK: store volatile
; CHECK: br i1 %again, label %body
  br label %body
body:
  store volatile i8 1, ptr %p
  br i1 %again, label %body, label %exit
exit:
  ret void
}

; Tagged application values must pass through unchanged.
define ptr @pointer_value(ptr %p, ptr %q, i1 %cond) {
; CHECK-LABEL: define ptr @pointer_value
; CHECK: %s = select i1 %cond, ptr %p, ptr %q
; CHECK: ret ptr %s
  %s = select i1 %cond, ptr %p, ptr %q
  ret ptr %s
}


; An existing spatial marker must not suppress a missing temporal check.
define i8 @already_spatial(ptr %p) {
; CHECK-LABEL: define i8 @already_spatial
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 1, i32 0)
; CHECK-NEXT: %v = load i8
; CHECK-NOT: call void @__flexfat_check_temporal
  %v = load i8, ptr %p, !flexfat.instrumented !0
  ret i8 %v
}
!0 = !{}

define ptr @phi_value(ptr %p, ptr %q, i1 %cond) {
; CHECK-LABEL: define ptr @phi_value
; CHECK: %phi = phi ptr [ %p, %left ], [ %q, %right ]
; CHECK: call void @__flexfat_check_temporal(i64 {{.*}}, i64 8, i32 0)
; CHECK: %value = load ptr, ptr %phi
; CHECK: ret ptr %value
  br i1 %cond, label %left, label %right
left:
  br label %join
right:
  br label %join
join:
  %phi = phi ptr [ %p, %left ], [ %q, %right ]
  %value = load ptr, ptr %phi
  ret ptr %value
}

; CHECK-LABEL: define internal void @__flexfat_tbi_ctor
; CHECK: call void @__flexfat_tbi_abi_v1()
