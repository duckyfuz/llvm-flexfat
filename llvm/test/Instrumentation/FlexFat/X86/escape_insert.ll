; FlexFat Unit 15: ESCAPE_INSERT (info code 9). Inserting a pointer
; into an aggregate (insertvalue) or vector (insertelement) makes the
; pointer escape — the aggregate / vector can be returned or stored.
; Two functions: one for each insert variety. Opaque pointers leave
; the shape of these instructions unchanged from the LowFat 4.0 era.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-elide -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define {ptr, i32} @escape_via_insertvalue(ptr %p, i64 %i) {
entry:
  %q = getelementptr i8, ptr %p, i64 %i
  %agg = insertvalue {ptr, i32} undef, ptr %q, 0
  %agg2 = insertvalue {ptr, i32} %agg, i32 7, 1
  ret {ptr, i32} %agg2
}

define <4 x ptr> @escape_via_insertelement(ptr %p, i64 %i, <4 x ptr> %v) {
entry:
  %q = getelementptr i8, ptr %p, i64 %i
  %r = insertelement <4 x ptr> %v, ptr %q, i32 0
  ret <4 x ptr> %r
}

; CHECK-LABEL: define {{.*}} @escape_via_insertvalue
; CHECK: call void @lowfat_oob_error(i32 9, ptr %q, ptr %{{.*}})
;
; CHECK-LABEL: define {{.*}} @escape_via_insertelement
; CHECK: call void @lowfat_oob_error(i32 9, ptr %q, ptr %{{.*}})
