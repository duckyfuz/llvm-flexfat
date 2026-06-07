; FlexFat Unit 13: each exclusion case (thread-local, user-declared section,
; user-declared alignment > 16, oversized) stays untouched — no section
; rewrite, no alignment bump.
;
; RUN: opt < %s -passes=flexfat-globals -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; Thread-local: skipped.
@tls = thread_local global i32 0

; User-declared section: skipped.
@user_sec = global i32 0, section "my_special_section"

; User-declared alignment > 16: skipped.
@big_align = global i32 0, align 64

; Pure declaration (no body): skipped.
@decl = external global i32

; Size > LOWFAT_MAX_GLOBAL_ALLOC_SIZE = 64 MiB: a "too big" warning and skip.
; 65 MiB array = above the cap.
@too_big = global [68157440 x i8] zeroinitializer

; CHECK-DAG: @tls = thread_local global i32 0{{$}}
; CHECK-DAG: @user_sec = global i32 0, section "my_special_section"{{$}}
; CHECK-DAG: @big_align = global i32 0, align 64{{$}}
; CHECK-DAG: @decl = external global i32{{$}}
; CHECK-DAG: @too_big = global [68157440 x i8] zeroinitializer{{$}}

; None of the exclusions should carry a lowfat section.
; CHECK-NOT: section "lowfat_section_
