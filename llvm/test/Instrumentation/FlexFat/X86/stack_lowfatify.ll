; FlexFat Unit 12b: a fixed-size alloca whose address escapes (stored into
; another pointer) is lowfatified — replaced by a sized byte-array alloca with
; class-alignment, with a constant-offset mirror that all uses get RAUW'd to,
; and lifetime intrinsics are dropped. clzll(size=8) = 60, sizes[60]=16, so
; the alloca is widened from 8 to 16 bytes.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @sink(ptr)
declare void @llvm.lifetime.start.p0(i64, ptr)
declare void @llvm.lifetime.end.p0(i64, ptr)

define void @escape_via_store(ptr %p) {
entry:
  %a = alloca i64, align 8
  call void @llvm.lifetime.start.p0(i64 8, ptr %a)
  store ptr %a, ptr %p, align 8         ; escape: address stored
  store i64 7, ptr %a, align 8          ; self-store (not an escape)
  call void @llvm.lifetime.end.p0(i64 8, ptr %a)
  ret void
}

; CHECK-LABEL: define void @escape_via_store(ptr %p)
;
; alloca is replaced by a byte-array alloca of newSize=16 (sizes[60]) with
; class alignment 16 (~masks[60]+1). The mirror is a constant-offset gep on
; the new alloca, marked with !flexfat.stack.mirror metadata so calcBasePtr
; recognizes the result as a fat (stack) pointer.
; CHECK:      %{{[^ ]+}} = alloca i8, i64 16, align 16
; CHECK:      getelementptr i8, ptr %{{[^,]+}}, i64 -2095944040448{{.*}}!flexfat.stack.mirror
;
; The escape now stores the MIRROR (a fat pointer), not the raw alloca:
; CHECK:      store ptr %{{[^,]+}}, ptr %p
;
; Lifetime intrinsic CALLs on the alloca are dropped (the alloca's size
; changed). (Module-level declarations may remain; we only care about uses.)
; CHECK-NOT:  call void @llvm.lifetime.start
; CHECK-NOT:  call void @llvm.lifetime.end
