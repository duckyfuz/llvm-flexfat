; RUN: opt -passes='flexfat<tbi>,flexfat<tbi>,verify' -S %s | FileCheck %s --implicit-check-not='__flexfat_temporal_regions' --implicit-check-not='call void @__flexfat_check_temporal' --implicit-check-not='udiv ' --implicit-check-not='sdiv '
; RUN: %if flexfat-custom-config %{ opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s --check-prefix=CUSTOM %}
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s --check-prefix=POW2 %}

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

; The spatial marker must not suppress temporal coverage. There is one failure
; branch and no managed/unmanaged or descriptor-slot-count branch.
define i8 @access(ptr %p) {
; CHECK-LABEL: define i8 @access(
; CHECK: [[TAGGED:%.*]] = ptrtoint ptr %p to i64
; CHECK: [[RAW:%.*]] = and i64 [[TAGGED]], 72057594037927935
; CHECK: icmp ult i64 [[RAW]], 281474976710656
; CHECK: select i1 {{.*}}, i64 {{.*}}, i64 0
; CHECK-NOT: br i1
; CHECK: [[MANAGED:%flexfat.managed]] = icmp ne i64 {{.*}}, -1
; CHECK: [[SLOT:%flexfat.metadata.slot]] = select i1 [[MANAGED]], i64 {{.*}}, i64 0
; CHECK: [[BIAS:%.*]] = load i64, ptr {{.*}}, align 8, !invariant.load
; CHECK: [[ADDRESS:%.*]] = add i64 [[BIAS]], [[SLOT]]
; CHECK: [[ENTRY:%.*]] = inttoptr i64 [[ADDRESS]] to ptr
; CHECK: [[GEN:%.*]] = load atomic i8, ptr [[ENTRY]] acquire, align 1, !nosanitize
; CHECK-NOT: !invariant.load
; CHECK: lshr i64 [[TAGGED]], 56
; CHECK: icmp ne i8 {{.*}}, 0
; CHECK: icmp eq i8 {{.*}}, [[GEN]]
; CHECK: xor i1 [[MANAGED]], true
; CHECK: or i1
; CHECK: br i1
; CHECK: [[OBS:%.*]] = zext i8 [[GEN]] to i32
; CHECK: call void @__flexfat_report_temporal_v3(i64 [[TAGGED]], i64 1, i32 0, i32 [[OBS]])
; CHECK-NEXT: unreachable
; CHECK-NOT: br i1
; CHECK-NOT: load atomic
; CHECK: %v = load i8, ptr %p,
; CHECK-NEXT: ret i8 %v
; CUSTOM-LABEL: define i8 @access(
; CUSTOM: mul i128
; CUSTOM: icmp ugt i64
; CUSTOM: %flexfat.slot = sub i64
; CUSTOM-NOT: mul i128
; CUSTOM: load atomic i8
; POW2-LABEL: define i8 @access(
; POW2: add i64 {{.*}}, 4
; POW2: [[SHIFT:%.*]] = select i1 {{.*}}, i64 {{.*}}, i64 0
; POW2-NEXT: %flexfat.slot = lshr i64 %flexfat.raw, [[SHIFT]]
  %v = load i8, ptr %p, !flexfat.instrumented !0
  ret i8 %v
}

declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
define void @dynamic_zero(ptr %p, i64 %n) {
; CHECK-LABEL: define void @dynamic_zero(
; CHECK: [[NZ:%.*]] = icmp ne i64 %n, 0
; CHECK-NEXT: br i1 [[NZ]]
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal_v3(i64 {{.*}}, i64 %n, i32 1,
; CHECK: call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 %n, i1 false)
  call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 %n, i1 false), !flexfat.instrumented !0
  ret void
}

define void @constant_zero(ptr %p) {
; CHECK-LABEL: define void @constant_zero(
; CHECK-NOT: load atomic
; CHECK-NOT: __flexfat_report_temporal_v3
; CHECK: ret void
  call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 0, i1 false), !flexfat.instrumented !0
  ret void
}

@global = global i8 0
define i8 @unmanaged() {
; CHECK-LABEL: define i8 @unmanaged(
; CHECK-NOT: load atomic
; CHECK-NOT: call void @__flexfat_report_temporal_v3
; CHECK: ret i8 %v
  %v = load i8, ptr @global
  ret i8 %v
}

; An incoming old constructor cannot suppress the retained v3 ABI reference.
define internal void @__flexfat_tbi_ctor() {
  call void @__flexfat_tbi_abi_v1()
  ret void
}
declare void @__flexfat_tbi_abi_v1()
; CHECK-LABEL: define internal void @__flexfat_tbi_ctor_v3()
; CHECK: call void @__flexfat_tbi_abi_v3()
; CHECK-NEXT: ret void
!0 = !{}
