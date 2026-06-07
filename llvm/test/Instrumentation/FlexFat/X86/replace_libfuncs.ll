; FlexFat Unit 9: replaceUnsafeLibFuncs rewrites calls to the unsafe libc
; functions to their lowfat_* equivalents. mem-intrinsics (memcpy/memset/
; memmove) are ALWAYS replaced; the allocator family (malloc/free/calloc/...,
; C++ new/delete) is replaced unless -flexfat-no-replace-malloc.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s
; RUN: opt < %s -passes=flexfat -flexfat-no-replace-malloc -S | FileCheck %s --check-prefix=NOREPL

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare ptr @memcpy(ptr, ptr, i64)
declare ptr @memset(ptr, i32, i64)
declare ptr @memmove(ptr, ptr, i64)
declare ptr @malloc(i64)
declare void @free(ptr)
declare ptr @calloc(i64, i64)
declare ptr @realloc(ptr, i64)
declare ptr @_Znwm(i64)

define void @f(ptr %d, ptr %s, i64 %n, ptr %p) {
  %a = call ptr @memcpy(ptr %d, ptr %s, i64 %n)
  %b = call ptr @memset(ptr %d, i32 0, i64 %n)
  %c = call ptr @memmove(ptr %d, ptr %s, i64 %n)
  %m = call ptr @malloc(i64 %n)
  call void @free(ptr %p)
  %cl = call ptr @calloc(i64 %n, i64 4)
  %r = call ptr @realloc(ptr %p, i64 %n)
  %nw = call ptr @_Znwm(i64 %n)
  ret void
}

; CHECK-LABEL: define void @f(
; CHECK:      call ptr @lowfat_memcpy(
; CHECK:      call ptr @lowfat_memset(
; CHECK:      call ptr @lowfat_memmove(
; CHECK:      call ptr @lowfat_malloc(
; CHECK:      call void @lowfat_free(
; CHECK:      call ptr @lowfat_calloc(
; CHECK:      call ptr @lowfat_realloc(
; CHECK:      call ptr @lowfat__Znwm(

; With -flexfat-no-replace-malloc the mem-intrinsics are still replaced, but the
; allocator family is left untouched.
; NOREPL-LABEL: define void @f(
; NOREPL:      call ptr @lowfat_memcpy(
; NOREPL:      call ptr @lowfat_memset(
; NOREPL:      call ptr @lowfat_memmove(
; NOREPL:      call ptr @malloc(
; NOREPL:      call void @free(
; NOREPL:      call ptr @calloc(
; NOREPL:      call ptr @realloc(
; NOREPL:      call ptr @_Znwm(
