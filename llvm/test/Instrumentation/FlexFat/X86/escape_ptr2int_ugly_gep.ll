; FlexFat Unit 15: the "ugly GEP" carve-out (verbatim port of
; LowFat.cpp:854-863). ptrtoint of a GEP tagged with `!uglygep`
; metadata is deliberately NOT instrumented for escape, to avoid the
; false positives the reference documented. This is a FALSIFIABLE
; port — if a future change drops the metadata check, the test below
; starts emitting an escape check on the ptrtoint and fails.
;
; (The metadata is set by InstCombine on LLVM 4.0 when canonicalising
; a GEP into a byte-offset form whose stride no longer matches the
; original element type. Modern LLVM rarely sets it in practice;
; preserving the carve-out keeps the FlexFat pass's semantics
; identical to the reference even if/when a future pipeline
; reintroduces the tag.)
;
; RUN: opt < %s -passes=flexfat -flexfat-no-elide -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @escape_ptr2int_ugly(ptr %p, i64 %i, ptr %sink_intptr) {
entry:
  ; The GEP carries `!uglygep` metadata. The escape predicate must skip
  ; the ptrtoint that derives from it.
  %q = getelementptr i8, ptr %p, i64 %i, !uglygep !0
  %int = ptrtoint ptr %q to i64
  store i64 %int, ptr %sink_intptr, align 8
  ret void
}

; The ptr2int int-escape would normally fire (the int is stored to memory),
; but because %q carries !uglygep, the ESCAPE_PTR2INT check (info=8) is
; suppressed.
; CHECK-LABEL: define void @escape_ptr2int_ugly
; CHECK-NOT: call void @lowfat_oob_error(i32 8

!0 = !{}
