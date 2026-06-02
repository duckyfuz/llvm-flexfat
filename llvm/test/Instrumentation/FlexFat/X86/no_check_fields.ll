; FlexFat Unit 8: -flexfat-no-check-fields flips an input pointer's trusted
; bounds from [0,0] (a constant field GEP is unprovable -> checked) to the size
; of the indexed object (the field is in bounds -> elided). With opaque pointers
; there is no pointee type, so the trusted size comes from the GEP's source
; element type (the indexed struct) rather than the reference's sizeof(*ptr).
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s --check-prefix=DEFAULT
; RUN: opt < %s -passes=flexfat -flexfat-no-check-fields -S | FileCheck %s --check-prefix=FIELDS

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

%struct.S = type { i64, i64, i64 } ; 24 bytes

define void @field(ptr %p) {
  %q = getelementptr %struct.S, ptr %p, i64 0, i32 1 ; offset 8, within 24
  store i64 0, ptr %q
  ret void
}

; By default the field access off an input pointer is checked:
; DEFAULT-LABEL: define void @field(
; DEFAULT:       call void @lowfat_oob_error
;
; With -flexfat-no-check-fields the input pointer is trusted up to the indexed
; object, so the in-bounds field access is elided:
; FIELDS-LABEL: define void @field(
; FIELDS-NOT:   call void @lowfat_oob_error
