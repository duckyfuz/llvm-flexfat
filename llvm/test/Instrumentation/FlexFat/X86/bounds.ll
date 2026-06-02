; FlexFat Unit 8: static bounds analysis elides provably in-bounds checks, while
; genuine OOB / unprovable accesses still get a check. The asymmetry is the
; point: positives assert the check is GONE, negatives assert it REMAINS.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; malloc carries the attributes clang emits, so getObjectSize can recover the
; allocation size (allocsize(0)) and isAllocationFn recognizes it (allockind).
declare noalias ptr @malloc(i64) allockind("alloc,uninitialized") allocsize(0) "alloc-family"="malloc"
@g = global [16 x i8] zeroinitializer

; POSITIVE: constant in-bounds GEP off a known-size heap object -> elided.
define void @malloc_inbounds() {
  %p = call ptr @malloc(i64 64)
  %q = getelementptr i8, ptr %p, i64 8
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @malloc_inbounds(
; CHECK-NOT:   call void @lowfat_oob_error

; POSITIVE: alloca of known size, accessed within -> elided.
define void @alloca_inbounds() {
  %a = alloca [16 x i8]
  %q = getelementptr i8, ptr %a, i64 4
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @alloca_inbounds(
; CHECK-NOT:   call void @lowfat_oob_error

; POSITIVE: global of known size, accessed within -> elided.
define void @global_inbounds() {
  %q = getelementptr i8, ptr @g, i64 4
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @global_inbounds(
; CHECK-NOT:   call void @lowfat_oob_error

; POSITIVE: select merge of known-size objects, access within the min -> elided.
define void @select_merge(i1 %c) {
  %m1 = call ptr @malloc(i64 64)
  %m2 = call ptr @malloc(i64 32)
  %p = select i1 %c, ptr %m1, ptr %m2
  %q = getelementptr i8, ptr %p, i64 8
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @select_merge(
; CHECK-NOT:   call void @lowfat_oob_error

; POSITIVE: PHI merge of known-size objects, access within the min -> elided.
define void @phi_merge(i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  %m1 = call ptr @malloc(i64 64)
  br label %merge
b:
  %m2 = call ptr @malloc(i64 32)
  br label %merge
merge:
  %p = phi ptr [ %m1, %a ], [ %m2, %b ]
  %q = getelementptr i8, ptr %p, i64 8
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @phi_merge(
; CHECK-NOT:   call void @lowfat_oob_error

; NEGATIVE: constant OOB GEP off a known-size heap object -> checked.
define void @malloc_oob() {
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 100
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @malloc_oob(
; CHECK:       call void @lowfat_oob_error

; NEGATIVE: dynamic GEP off an input pointer (offset unprovable) -> checked.
define void @dynamic(ptr %p, i64 %i) {
  %q = getelementptr i8, ptr %p, i64 %i
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @dynamic(
; CHECK:       call void @lowfat_oob_error

; NEGATIVE: unknown-provenance input pointer (loaded) + positive offset -> checked.
define void @load_input(ptr %pp) {
  %p = load ptr, ptr %pp
  %q = getelementptr i8, ptr %p, i64 8
  store i8 0, ptr %q
  ret void
}
; CHECK-LABEL: define void @load_input(
; CHECK:       call void @lowfat_oob_error
