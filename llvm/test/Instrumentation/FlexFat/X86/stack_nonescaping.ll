; FlexFat Unit 12b: an alloca with only load/cmp/self-store uses (the address
; never escapes the function) stays native — no mirror, no replacement. The
; static-bounds analysis from Unit 8 already covers its direct accesses.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @nonescaping(i32 %x) {
entry:
  %a = alloca i32, align 4
  store i32 %x, ptr %a, align 4       ; self-store as address (NOT an escape)
  %v = load i32, ptr %a, align 4
  %c = icmp eq i32 %v, 42
  %sel = select i1 %c, i32 %v, i32 0
  ret i32 %sel
}

; CHECK-LABEL: define i32 @nonescaping(i32 %x)
; The alloca keeps its original shape and no mirror is inserted.
; CHECK:      alloca i32, align 4
; CHECK-NOT:  flexfat.stack.mirror
; CHECK-NOT:  alloca i8, i64
