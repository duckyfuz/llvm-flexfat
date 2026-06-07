; FlexFat Unit 13: a Common-linkage global is **promoted to WeakAny** before
; sectioning (the linker ignores the section attribute on Common-linkage
; symbols and places them in BSS, so the section attribute would otherwise
; be silently dropped).
;
; RUN: opt < %s -passes=flexfat-globals -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@comm = common global i32 0, align 4

; CHECK: @comm = weak global i32 0, section "lowfat_section_16", align 16
