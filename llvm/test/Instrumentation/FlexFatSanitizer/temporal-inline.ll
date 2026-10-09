; RUN: opt -passes='flexfat<tbi>,flexfat<tbi>,verify' -S %s | FileCheck %s --implicit-check-not='call void @__flexfat_check_temporal' --implicit-check-not='load atomic i8' --implicit-check-not='udiv ' --implicit-check-not='sdiv ' --implicit-check-not='urem ' --implicit-check-not='srem '
; RUN: %if flexfat-custom-config %{ opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s --check-prefix=CUSTOM %}
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s --check-prefix=POW2 %}

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

; Existing spatial markers isolate the temporal CFG and demonstrate that they
; do not suppress temporal coverage. Every generated memory access is nosanitize.
define i8 @access(ptr %p) {
; CHECK-LABEL: define i8 @access(
; CHECK: [[TAGGED:%.*]] = ptrtoint ptr %p to i64
; CHECK: [[RAW:%.*]] = and i64 [[TAGGED]], 72057594037927935
; CHECK: [[OFF:%.*]] = sub i64 [[RAW]],
; CHECK: [[MANAGED:%.*]] = icmp ult i64 [[OFF]],
; CHECK: br i1 [[MANAGED]], label %flexfat.temporal.geometry, label %flexfat.temporal.cont
; CHECK: flexfat.temporal.geometry:
; CHECK: getelementptr inbounds [{{[0-9]+}} x { i64, i64, i64 }], ptr @__flexfat_temporal_regions_v2, i64 0, i64 %flexfat.temporal.class
; CHECK: [[FIRST:%.*]] = load i64, ptr {{.*}}, align 8, !nosanitize
; CHECK: [[COUNT:%.*]] = load i64, ptr {{.*}}, align 8, !nosanitize
; CHECK: [[INDEX:%.*]] = sub i64 {{.*}}, [[FIRST]]
; CHECK: [[VALID:%.*]] = icmp ult i64 [[INDEX]], [[COUNT]]
; CHECK: br i1 [[VALID]], label %flexfat.temporal.observe, label %flexfat.temporal.failure, !prof
; CHECK: flexfat.temporal.observe:
; CHECK: [[BASE:%.*]] = load i64, ptr {{.*}}, align 8, !nosanitize
; CHECK: [[PTR:%.*]] = inttoptr i64 [[BASE]] to ptr
; CHECK: [[ENTRY:%.*]] = getelementptr inbounds i8, ptr [[PTR]], i64 [[INDEX]]
; CHECK: [[GEN:%.*]] = load atomic i8, ptr [[ENTRY]] acquire, align 1, !nosanitize
; CHECK: lshr i64 [[TAGGED]], 56
; CHECK-DAG: icmp ne i8 {{.*}}, 0
; CHECK-DAG: icmp eq i8 {{.*}}, [[GEN]]
; CHECK: br i1 {{.*}}, label %flexfat.temporal.cont, label %flexfat.temporal.failure, !prof
; CHECK: flexfat.temporal.failure:
; CHECK: [[FAILED:%.*]] = phi i8 [ 0, %flexfat.temporal.geometry ], [ [[GEN]], %flexfat.temporal.observe ]
; CHECK: [[SLOT:%.*]] = phi i32 [ 0, %flexfat.temporal.geometry ], [ 1, %flexfat.temporal.observe ]
; CHECK: [[OBS:%.*]] = zext i8 [[FAILED]] to i32
; CHECK: call void @__flexfat_report_temporal(i64 [[TAGGED]], i64 1, i32 0, i32 [[OBS]], i32 [[SLOT]])
; CHECK-NEXT: unreachable
; CHECK: flexfat.temporal.cont:
; CHECK-NEXT: %v = load i8, ptr %p,
; CHECK-NEXT: ret i8 %v
; CUSTOM-LABEL: define i8 @access(
; CUSTOM: lshr i64 %flexfat.temporal.raw,
; CUSTOM: load i64
; CUSTOM: load i64
; CUSTOM: mul i128
; CUSTOM: lshr i128 {{.*}}, 64
; CUSTOM: trunc i128 {{.*}} to i64
; CUSTOM: mul i64
; CUSTOM: icmp ugt i64
; CUSTOM: zext i1 {{.*}} to i64
; CUSTOM: %flexfat.slot = sub i64
; POW2-LABEL: define i8 @access(
; POW2: [[SHIFT:%.*]] = add i64 %flexfat.temporal.class, 4
; POW2: %flexfat.temporal.slot = lshr i64 %flexfat.temporal.raw, [[SHIFT]]
  %v = load i8, ptr %p, !flexfat.instrumented !0
  ret i8 %v
}

declare void @llvm.memset.p0.i64(ptr, i8, i64, i1 immarg)
define void @dynamic_zero(ptr %p, i64 %n) {
; CHECK-LABEL: define void @dynamic_zero(
; CHECK: [[NZ:%.*]] = icmp ne i64 %n, 0
; CHECK: [[CHECK:%.*]] = and i1 {{.*}}, [[NZ]]
; CHECK: br i1 [[CHECK]], label %flexfat.temporal.geometry, label %flexfat.temporal.cont
; CHECK: load atomic i8
; CHECK: call void @__flexfat_report_temporal(i64 {{.*}}, i64 %n, i32 1,
; CHECK: call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 %n, i1 false)
  call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 %n, i1 false), !flexfat.instrumented !0
  ret void
}

define void @constant_zero(ptr %p) {
; CHECK-LABEL: define void @constant_zero(
; CHECK-NOT: flexfat.temporal.geometry
; CHECK-NOT: load atomic
; CHECK: call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 0, i1 false)
; CHECK-NEXT: ret void
  call void @llvm.memset.p0.i64(ptr %p, i8 0, i64 0, i1 false), !flexfat.instrumented !0
  ret void
}

@global = global i8 0
define i8 @unmanaged() {
; CHECK-LABEL: define i8 @unmanaged(
; CHECK-NOT: load atomic
; CHECK-NOT: call void @__flexfat_report_temporal
; CHECK: ret i8 %v
  %v = load i8, ptr @global
  ret i8 %v
}

; A v1 ctor in incoming IR must not prevent the v2 ABI requirement.
define internal void @__flexfat_tbi_ctor() {
  call void @__flexfat_tbi_abi_v1()
  ret void
}
declare void @__flexfat_tbi_abi_v1()
; CHECK-LABEL: define internal void @__flexfat_tbi_ctor_v2()
; CHECK: call void @__flexfat_tbi_abi_v2()
; CHECK-NEXT: ret void
!0 = !{}
