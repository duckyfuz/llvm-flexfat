; RUN: opt -passes='default<O3>,flexfat,function(early-cse<memssa>,instcombine,simplifycfg),verify' -disable-output %s
; RUN: opt -passes='flexfat,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat,flexfat,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat,default<O2>,verify' -disable-output %s

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"
declare ptr @malloc(i64)

; Optimizer-last can encounter a pointer vector converted to escaping integers.
; Each pointer needs its own scalar geometry, rather than a vector/scalar cast.
define <2 x i64> @fixed_escape(ptr %base, <2 x i64> %offsets) {
; CHECK-LABEL: define <2 x i64> @fixed_escape(
; CHECK: [[IDX0:%.*]] = extractelement <2 x i64> %offsets, i64 0
; CHECK: [[P0:%.*]] = getelementptr i8, ptr %base, i64 [[IDX0]]
; CHECK: ptrtoint ptr [[P0]] to i64
; CHECK: call void @__flexfat_report_oob(
; CHECK: [[IDX1:%.*]] = extractelement <2 x i64> %offsets, i64 1
; CHECK: [[P1:%.*]] = getelementptr i8, ptr %base, i64 [[IDX1]]
; CHECK: ptrtoint ptr [[P1]] to i64
; CHECK: call void @__flexfat_report_oob(
; CHECK: %v = ptrtoint <2 x ptr> %p to <2 x i64>
; CHECK: ret <2 x i64> %v
  %p = getelementptr i8, ptr %base, <2 x i64> %offsets
  %v = ptrtoint <2 x ptr> %p to <2 x i64>
  ret <2 x i64> %v
}

define <vscale x 2 x i64> @scalable_escape(ptr %base, <vscale x 2 x i64> %offsets) {
; CHECK-LABEL: define <vscale x 2 x i64> @scalable_escape(
; CHECK: call i64 @llvm.vscale.i64()
; CHECK: flexfat.escape.loop:
; CHECK: [[IDX:%.*]] = phi i64
; CHECK: [[OFFSET:%.*]] = extractelement <vscale x 2 x i64> %offsets, i64 [[IDX]]
; CHECK: [[P:%.*]] = getelementptr i8, ptr %base, i64 [[OFFSET]]
; CHECK: ptrtoint ptr [[P]] to i64
; CHECK: call void @__flexfat_report_oob(
; CHECK: br i1 {{.*}}, label %flexfat.escape.loop, label %flexfat.escape.continue
; CHECK: flexfat.escape.continue:
; CHECK: %v = ptrtoint <vscale x 2 x ptr> %p to <vscale x 2 x i64>
; CHECK: ret <vscale x 2 x i64> %v
  %p = getelementptr i8, ptr %base, <vscale x 2 x i64> %offsets
  %v = ptrtoint <vscale x 2 x ptr> %p to <vscale x 2 x i64>
  ret <vscale x 2 x i64> %v
}

; Preserve each lane's allocation base through an incoming vector PHI.
define <2 x i64> @joined_escape(i1 %which, i64 %offset) {
; CHECK-LABEL: define <2 x i64> @joined_escape(
; CHECK: flexfat.escape.phi
; CHECK: call void @__flexfat_report_oob(
; CHECK: call void @__flexfat_report_oob(
; CHECK: %bits = ptrtoint <2 x ptr> %joined to <2 x i64>
; CHECK: ret <2 x i64> %bits
  %a = call ptr @malloc(i64 16)
  %b = call ptr @malloc(i64 32)
  %pa = getelementptr i8, ptr %a, i64 %offset
  %pb = getelementptr i8, ptr %b, i64 %offset
  %a0 = insertelement <2 x ptr> poison, ptr %pa, i64 0
  %av = insertelement <2 x ptr> %a0, ptr %pb, i64 1
  %b0 = insertelement <2 x ptr> poison, ptr %pb, i64 0
  %bv = insertelement <2 x ptr> %b0, ptr %pa, i64 1
  br i1 %which, label %left, label %right
left:
  br label %merge
right:
  br label %merge
merge:
  %joined = phi <2 x ptr> [%av, %left], [%bv, %right]
  %bits = ptrtoint <2 x ptr> %joined to <2 x i64>
  ret <2 x i64> %bits
}

; Duplicate switch edges must also agree in the scalar lane PHIs.
define <2 x i64> @duplicate_edges(ptr %base, <2 x i64> %offsets, i32 %which) {
; CHECK-LABEL: define <2 x i64> @duplicate_edges(
; CHECK: flexfat.escape.phi = phi ptr
; CHECK: flexfat.base = phi ptr
; CHECK: flexfat.escape.phi{{[0-9]+}} = phi ptr
; CHECK: ret <2 x i64>
entry:
  %p = getelementptr i8, ptr %base, <2 x i64> %offsets
  switch i32 %which, label %exit [ i32 0, label %join
                                  i32 1, label %join ]
join:
  %joined = phi <2 x ptr> [ %p, %entry ], [ %p, %entry ]
  %bits = ptrtoint <2 x ptr> %joined to <2 x i64>
  ret <2 x i64> %bits
exit:
  ret <2 x i64> zeroinitializer
}

; Scalarize the select as well as its lane-wise GEP operands.
define <2 x i64> @selected_escape(<2 x i1> %choose, ptr %a, ptr %b,
                                 <2 x i64> %offsets) {
; CHECK-LABEL: define <2 x i64> @selected_escape(
; CHECK: select i1
; CHECK: call void @__flexfat_report_oob(
; CHECK: select i1
; CHECK: call void @__flexfat_report_oob(
; CHECK: ret <2 x i64>
  %left = getelementptr i8, ptr %a, <2 x i64> %offsets
  %right = getelementptr i8, ptr %b, <2 x i64> %offsets
  %chosen = select <2 x i1> %choose, <2 x ptr> %left, <2 x ptr> %right
  %bits = ptrtoint <2 x ptr> %chosen to <2 x i64>
  ret <2 x i64> %bits
}

declare <2 x ptr> @may_throw()
declare i32 @personality(...)
; Extraction of an invoke result must stay on its normal edge.
define <2 x i64> @invoke_escape(i1 %choose) personality ptr @personality {
; CHECK-LABEL: define <2 x i64> @invoke_escape(
; CHECK: invoke <2 x ptr> @may_throw()
; CHECK: extractelement <2 x ptr> %p
; CHECK: flexfat.escape.phi = phi ptr
; CHECK: flexfat.base = phi ptr
; CHECK: flexfat.escape.phi{{[0-9]+}} = phi ptr
; CHECK: ret <2 x i64>
entry:
  br i1 %choose, label %call, label %other
call:
  %p = invoke <2 x ptr> @may_throw() to label %join unwind label %unwind
other:
  br label %join
join:
  %joined = phi <2 x ptr> [ %p, %call ], [ zeroinitializer, %other ]
  %bits = ptrtoint <2 x ptr> %joined to <2 x i64>
  ret <2 x i64> %bits
unwind:
  %landing = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %landing
}
