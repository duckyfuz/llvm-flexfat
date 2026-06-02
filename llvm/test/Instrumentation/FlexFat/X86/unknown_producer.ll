; FlexFat Unit 8 (finding 3): a pointer of untraceable provenance -- a function
; argument or an opaque external call result -- dereferenced at offset 0 is
; TRUSTED, and its check ELIDED. Finding 3 confirmed the reference elides here
; (recognized input pointers get getInputPtrBounds -> [0,0], in-bounds at offset
; 0; truly-unrecognized producers fall through to NONFAT). This is a deliberate
; detection/overhead tradeoff inherited from the reference: a *direct* deref of
; an already-out-of-bounds pointer is NOT caught (see docs/STATUS.md). Only
; positive offsets off such a pointer are checked.
;
; This file is also a corpus canary: none of these recognized producers may trip
; the unrecognized-producer fallback (--implicit-check-not="unknown pointer").
;
; RUN: opt < %s -passes=flexfat -S 2>&1 | FileCheck %s --implicit-check-not="unknown pointer"

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare ptr @ext()

; Function argument, direct deref -> elided (untraceable provenance, offset 0).
define i8 @arg_direct(ptr %p) {
  %v = load i8, ptr %p
  ret i8 %v
}
; CHECK-LABEL: define i8 @arg_direct(
; CHECK-NOT:   call void @lowfat_oob_error

; Opaque external call result, direct deref -> elided.
define i8 @extcall_direct() {
  %p = call ptr @ext()
  %v = load i8, ptr %p
  ret i8 %v
}
; CHECK-LABEL: define i8 @extcall_direct(
; CHECK-NOT:   call void @lowfat_oob_error

; inttoptr, direct deref -> elided.
define i8 @inttoptr_direct(i64 %x) {
  %p = inttoptr i64 %x to ptr
  %v = load i8, ptr %p
  ret i8 %v
}
; CHECK-LABEL: define i8 @inttoptr_direct(
; CHECK-NOT:   call void @lowfat_oob_error

; CONTRAST: a positive offset off the same untraceable argument IS checked --
; the trust is only the byte at offset 0, not the object.
define i8 @arg_offset(ptr %p) {
  %q = getelementptr i8, ptr %p, i64 8
  %v = load i8, ptr %q
  ret i8 %v
}
; CHECK-LABEL: define i8 @arg_offset(
; CHECK:       call void @lowfat_oob_error
