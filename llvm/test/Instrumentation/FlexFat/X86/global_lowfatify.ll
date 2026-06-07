; FlexFat Unit 13: an eligible mutable global gets alignment set to the
; size-class boundary and `section "lowfat_section_<size>"`. Eligible const
; globals get `section "lowfat_section_const_<size>"`. Eligibility predicate
; is isInterestingGlobal (no user section, align <= 16, not thread-local,
; ordinary linkage). Class size folds at compile time from kStackSizes[].
;
; RUN: opt < %s -passes=flexfat-globals -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

; A mutable 32-byte global (i64[4]). clzll(32)=58, sizes[58]=64. Section is
; lowfat_section_64. Alignment is bumped from default to 64 (~masks[58]+1).
@mut = global [4 x i64] zeroinitializer

; A const 4-byte global (i32). clzll(4)=61, sizes[61]=16. Section is
; lowfat_section_const_16. Alignment 16.
@cst = constant i32 42

; CHECK-DAG: @mut = global [4 x i64] zeroinitializer, section "lowfat_section_64", align 64
; CHECK-DAG: @cst = constant i32 42, section "lowfat_section_const_16", align 16
