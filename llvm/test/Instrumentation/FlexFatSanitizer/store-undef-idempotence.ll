; RUN: opt < %s -passes='function(require<domtree>),flexfat,function(verify<domtree>),verify' -debug-pass-manager -disable-output 2>&1 | FileCheck %s --check-prefix=ANALYSES
; RUN: opt < %s -passes='function(require<domtree>),flexfat,function(verify<domtree>),verify' -S -o %t.once
; RUN: FileCheck %s < %t.once
; RUN: opt < %s -passes='flexfat,flexfat,verify' -S -o %t.twice
; RUN: diff %t.once %t.twice
; RUN: opt < %s -passes='flexfat<whole-access>,verify' -S -o %t.whole.once
; RUN: FileCheck %s < %t.whole.once
; RUN: opt < %s -passes='flexfat<whole-access>,flexfat<whole-access>,verify' -S -o %t.whole.twice
; RUN: diff %t.whole.once %t.whole.twice

target triple = "aarch64-unknown-linux-gnu"

@slot = global ptr null
@value = global i8 0

define void @conditional_escape(ptr %input, i1 %take) {
; CHECK-LABEL: @conditional_escape(
; CHECK: [[COND:%[^ ]+]] = freeze i1 %take
; CHECK: %flexfat.escape.pointer = select i1 [[COND]], ptr null, ptr %q
; CHECK: %flexfat.escape.defined = select i1 [[COND]], i1 false, i1 true
; CHECK: br i1 %flexfat.escape.defined,
; CHECK: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr @slot, align 8, !flexfat.instrumented ![[MARK:[0-9]+]]
; CHECK: ret void
  %q = getelementptr i8, ptr %input, i64 1
  %p = select i1 %take, ptr undef, ptr %q
  store ptr %p, ptr @slot
  ret void
}

define void @elided_escape(i1 %take) {
; CHECK-LABEL: @elided_escape(
; CHECK: [[COND:%[^ ]+]] = freeze i1 %take
; CHECK: %flexfat.escape.pointer = select i1 [[COND]], ptr null, ptr @value
; CHECK: %flexfat.escape.defined = select i1 [[COND]], i1 false, i1 true
; CHECK: br i1 %flexfat.escape.defined,
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr @slot, align 8, !flexfat.instrumented ![[MARK]]
; CHECK: ret void
  %p = select i1 %take, ptr undef, ptr @value
  store ptr %p, ptr @slot
  ret void
}

; Helper-only IR changes still invalidate analyses and need an idempotence marker.
define void @undefined_derived_escape(i64 %offset) {
; CHECK-LABEL: @undefined_derived_escape(
; CHECK: %flexfat.escape.derived = getelementptr i8, ptr null, i64 %offset
; CHECK: %flexfat.escape.pointer = select i1 false, ptr %flexfat.escape.derived, ptr null
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: store ptr %p, ptr @slot, align 8, !flexfat.instrumented ![[MARK]]
; CHECK: ret void
  %p = getelementptr i8, ptr undef, i64 %offset
  store ptr %p, ptr @slot
  ret void
}

; ANALYSES: Running analysis: DominatorTreeAnalysis on undefined_derived_escape
; ANALYSES: Running pass: FlexFatSanitizerPass
; ANALYSES: Running pass: DominatorTreeVerifierPass on undefined_derived_escape
; ANALYSES: Running analysis: DominatorTreeAnalysis on undefined_derived_escape
