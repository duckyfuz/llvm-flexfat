; FlexFat Unit 12b: an alloca whose address escapes INDIRECTLY (through GEP,
; then store of the derived pointer) is still lowfatified — the recursive
; doesAllocaEscape walks through gep/bitcast/select/phi.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @sink(ptr)

define void @escape_via_gep(ptr %p) {
entry:
  %a = alloca [32 x i8], align 1
  %g = getelementptr i8, ptr %a, i64 4
  store ptr %g, ptr %p, align 8    ; escape via gep-derived pointer
  ret void
}

; CHECK-LABEL: define void @escape_via_gep(ptr %p)
; Size=32 ⇒ clzll(32)=58, sizes[58]=64, masks[58]=0xFFFFFFFFFFFFFFC0 ⇒ align 64.
; CHECK:      %{{[^ ]+}} = alloca i8, i64 64, align 64
; CHECK:      getelementptr i8, ptr %{{[^,]+}}, i64 -1992864825344{{.*}}!flexfat.stack.mirror
; The downstream GEP and its store ultimately operate off the mirror.
; CHECK:      store ptr %{{[^,]+}}, ptr %p
