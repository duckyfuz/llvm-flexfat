; Regression test for a Unit-12b doesAllocaEscape bug surfaced during
; Unit 14b: a `volatile` local alloca with NO real address-escape gets
; lowfatified anyway because `llvm.lifetime.start/end` intrinsics
; (left in IR by clang at -O>=1) fail FlexFat's doesAllocaEscape
; check — they're CallInsts with argmem effects, so the "Call ⇒
; doesNotAccessMemory else escape" branch returns true. After the
; spurious lowfatification, the mirror gep has a ~2 TB negative offset
; off the byte-array alloca; a downstream SROA pass can't reconcile
; that with the volatile load/store and folds the use chain (in the
; real reproducer at -O2 the whole function becomes `ret i32 poison`).
;
; This IR mirrors what clang emits for:
;   int main(int argc, char **argv) {
;     volatile unsigned int sentinel = 0xAAAAAAAA;
;     if (argc > 1) sentinel = 0xBBBBBBBB;
;     return (int)sentinel;
;   }
; at -O0 then with mem2reg blocked by `volatile` — i.e. exactly the
; pre-FlexFat-pass IR observed in the print-after-all trace.
;
; Pinned the finding as XFAIL initially; the fix landed in the same
; commit (doesAllocaEscape now treats lifetime_start/end as non-escape),
; so the XFAIL is removed and the CHECKs below are load-bearing.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @llvm.lifetime.start.p0(ptr nocapture)
declare void @llvm.lifetime.end.p0(ptr nocapture)

define i32 @repro(i32 %argc) {
entry:
  %sentinel = alloca i32, align 4
  call void @llvm.lifetime.start.p0(ptr %sentinel)
  store volatile i32 -1431655766, ptr %sentinel, align 4
  %cmp = icmp sgt i32 %argc, 1
  br i1 %cmp, label %if.then, label %if.end

if.then:
  store volatile i32 -1145324613, ptr %sentinel, align 4
  br label %if.end

if.end:
  %v = load volatile i32, ptr %sentinel, align 4
  call void @llvm.lifetime.end.p0(ptr %sentinel)
  ret i32 %v
}

; CHECK-LABEL: define i32 @repro
;
; The alloca must NOT be lowfatified: no byte-array replacement,
; no mirror gep, no flexfat.stack.mirror metadata. The volatile
; stores and load must be preserved on the original %sentinel ptr.
;
; CHECK:      %sentinel = alloca i32, align 4
; CHECK-NOT:  alloca i8, i64 16
; CHECK-NOT:  flexfat.stack.mirror
; CHECK:      store volatile i32 -1431655766, ptr %sentinel
; CHECK:      store volatile i32 -1145324613, ptr %sentinel
; CHECK:      load volatile i32, ptr %sentinel
