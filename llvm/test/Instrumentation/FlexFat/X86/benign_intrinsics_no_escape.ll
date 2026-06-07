; Unit-14b companion test: the doesAllocaEscape "Call ⇒ doesNotAccessMemory
; else escape" clause was tuned for clang/LLVM 4.0's intrinsic memory
; attributes. The previous regression
; (volatile_alloca_escape_bug.ll) caught `llvm.lifetime.start/end`
; falling through to a spurious escape under modern LLVM. This test
; pins the broader category: an alloca whose only "call" uses are
; benign annotation intrinsics — lifetime markers AND debug declares
; AND alignment-style assume(ptrtoint(&x) & N == 0) — MUST NOT cause
; lowfatification.
;
; The user-prompt era-drift watch: any future "everything is suddenly
; lowfatified" perf regression should suspect a new intrinsic falling
; through the Call clause, and grow this test to cover it.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @llvm.lifetime.start.p0(ptr nocapture)
declare void @llvm.lifetime.end.p0(ptr nocapture)
declare void @llvm.dbg.declare(metadata, metadata, metadata)
declare void @llvm.assume(i1)

; Subject: a local alloca that only has benign-annotation users plus a
; volatile load/store. Behaves like a stack variable the optimizer
; might ordinarily promote, except `volatile` keeps the alloca alive
; long enough for FlexFat's predicate to inspect it.
define i32 @benign_annotations(i32 %argc) {
entry:
  %x = alloca i32, align 4
  ; (1) lifetime markers — clang -O>=1 emits these on every alloca
  ;     that survives mem2reg.
  call void @llvm.lifetime.start.p0(ptr %x)
  ; (2) dbg.declare — debug info attached to the alloca.
  call void @llvm.dbg.declare(metadata ptr %x, metadata !2, metadata !DIExpression()), !dbg !3
  ; (3) assume on alignment via ptrtoint+icmp — a common idiom.
  %xint = ptrtoint ptr %x to i64
  %and = and i64 %xint, 3
  %cmp = icmp eq i64 %and, 0
  call void @llvm.assume(i1 %cmp)
  ; (4) volatile load/store — these self-stores and direct loads aren't
  ;     escapes; what we care about here is whether the call-use
  ;     classifier loses its head and counts (1)/(2)/(3) as escapes.
  store volatile i32 -1431655766, ptr %x, align 4
  %v = load volatile i32, ptr %x, align 4
  call void @llvm.lifetime.end.p0(ptr %x)
  ret i32 %v
}

; CHECK-LABEL: define i32 @benign_annotations
;
; The alloca must NOT be lowfatified — no byte-array swap, no mirror
; gep, no `flexfat.stack.mirror` metadata. The volatile load and store
; must still target the original alloca pointer.
;
; CHECK:     %x = alloca i32, align 4
; CHECK-NOT: alloca i8, i64 16
; CHECK-NOT: flexfat.stack.mirror
; CHECK:     store volatile i32 -1431655766, ptr %x
; CHECK:     load volatile i32, ptr %x

!llvm.module.flags = !{!0}
!llvm.dbg.cu = !{!1}
!0 = !{i32 2, !"Debug Info Version", i32 3}
!1 = distinct !DICompileUnit(language: DW_LANG_C99, file: !4, producer: "test", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug)
!2 = !DILocalVariable(name: "x", scope: !5, file: !4, line: 1, type: !6)
!3 = !DILocation(line: 1, scope: !5)
!4 = !DIFile(filename: "t.c", directory: "/")
!5 = distinct !DISubprogram(name: "f", scope: !4, file: !4, line: 1, type: !7, scopeLine: 1, spFlags: DISPFlagDefinition, unit: !1)
!6 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!7 = !DISubroutineType(types: !8)
!8 = !{}
