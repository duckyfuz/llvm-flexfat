; FlexFat Unit 13: -flexfat-no-replace-globals suppresses the whole
; transform — globals stay native, no section rewrite, no alignment bump.
; The Unit-10 forward-declared flag finally gets its behavioral test
; (the last of three: -no-replace-alloca lit in Unit 12b, this one now).
;
; RUN: opt < %s -passes=flexfat-globals -flexfat-no-replace-globals -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@g = global i32 0, align 4

; CHECK: @g = global i32 0, align 4
; CHECK-NOT: section "lowfat_section
