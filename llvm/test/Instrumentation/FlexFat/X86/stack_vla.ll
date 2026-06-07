; FlexFat Unit 12b: a variable-length alloca (VLA) goes through the runtime
; path — class index from llvm.ctlz.i64(size, true), inline table loads for
; lowfat_stack_{offset,allocsize}, inline mask-align (and-mask of ptrtoint),
; llvm.stackrestore, and a constant-offset mirror gep tagged
; !flexfat.stack.mirror.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare void @sink(ptr)

define void @vla(i64 %n, ptr %p) {
entry:
  %a = alloca i32, i64 %n     ; size unknown — VLA path
  store ptr %a, ptr %p, align 8
  ret void
}

; CHECK-LABEL: define void @vla(i64 %n, ptr %p)
; Compute total bytes = 4 * n, then idx = ctlz.i64(size, true).
; CHECK:      mul i64
; CHECK:      call i64 @llvm.ctlz.i64(i64 %{{.*}}, i1 true)
; Inline table loads (the lowfat_stack_{offset,allocsize,masks} arrays).
; Each is a GEP off the externally-declared global array, then a load.
; CHECK:      getelementptr [0 x i64], ptr @lowfat_stack_offsets
; CHECK-NEXT: load i64
; CHECK:      getelementptr [0 x i64], ptr @lowfat_stack_sizes
; CHECK-NEXT: load i64
; The replacement alloca and the inline "align" (mask-and + inttoptr) +
; stackrestore (matches the reference's lowfat_stack_align body).
; CHECK:      alloca i8, i64 %{{.*}}
; CHECK:      getelementptr [0 x i64], ptr @lowfat_stack_masks
; CHECK-NEXT: load i64
; CHECK:      and i64
; CHECK:      call void @llvm.stackrestore
; The mirror: a constant-offset gep on the aligned alloca pointer, tagged.
; CHECK:      getelementptr i8, ptr %{{.*}}, i64 %{{.*}}!flexfat.stack.mirror
