; RUN: opt < %s -passes='function(require<domtree>),flexfat,function(verify<domtree>),verify' -debug-pass-manager -disable-output 2>&1 | FileCheck %s

target triple = "aarch64-unknown-linux-gnu"

@slot = global ptr null

; Building a derived check-only pointer changes IR even when the predicate
; folds to false and neither a guard nor a bounds check is emitted.
; CHECK: Running analysis: DominatorTreeAnalysis on undef_derived_spill
; CHECK: Running pass: FlexFatSanitizerPass
; CHECK: Running pass: DominatorTreeVerifierPass on undef_derived_spill
; CHECK: Running analysis: DominatorTreeAnalysis on undef_derived_spill
define void @undef_derived_spill(i64 %offset) {
  %p = getelementptr i8, ptr undef, i64 %offset
  store ptr %p, ptr @slot
  ret void
}
