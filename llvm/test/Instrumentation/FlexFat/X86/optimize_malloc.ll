; FlexFat Unit 9: optimizeMalloc constant-folds the heap-size-class selection.
; A constant malloc(K) (after replaceUnsafeLibFuncs -> lowfat_malloc(K)) becomes
; lowfat_malloc_index(idx, K) with idx = heap_select(K) computed at compile time,
; saving the runtime clzll/lzcnt dispatch. A dynamic malloc(n) stays a plain
; lowfat_malloc call.
;
; Unit 17: CHECK lines bake the non-POW2 heap_select index mapping
; (e.g. malloc(16) -> lowfat_malloc_index(1, 16), malloc(48) -> idx 3).
; The POW2 variant has a different size schedule (only powers of 2; sizes
; 16/32/64/.../2^33), so the constant-folded indices differ.
; REQUIRES: flexfat-nonpow2
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare ptr @malloc(i64)

; malloc(100): smallest class >= 101 is 112 (region index 7).
define ptr @const_malloc() {
  %p = call ptr @malloc(i64 100)
  ret ptr %p
}
; CHECK-LABEL: define ptr @const_malloc(
; CHECK:      call ptr @lowfat_malloc_index(i64 7, i64 100)
; CHECK-NOT:  call ptr @lowfat_malloc(

define ptr @dyn_malloc(i64 %n) {
  %p = call ptr @malloc(i64 %n)
  ret ptr %p
}
; CHECK-LABEL: define ptr @dyn_malloc(
; CHECK:      call ptr @lowfat_malloc(i64 %n)
; CHECK-NOT:  call ptr @lowfat_malloc_index
