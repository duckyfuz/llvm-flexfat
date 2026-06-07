; FlexFat Unit 10: -flexfat-no-check-blacklist suppresses instrumentation for the
; functions/files listed in a SpecialCaseList file. The listed function emits no
; checks; an unlisted one in the same module still does.
;
; RUN: opt < %s -passes=flexfat -flexfat-no-check-blacklist=%S/Inputs/flexfat_blacklist.txt -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @no_check_me(ptr %p, i64 %i) {
  %q = getelementptr i8, ptr %p, i64 %i
  store i8 0, ptr %q
  ret void
}

define void @do_check_me(ptr %p, i64 %i) {
  %q = getelementptr i8, ptr %p, i64 %i
  store i8 0, ptr %q
  ret void
}

; CHECK-LABEL: define void @no_check_me(
; CHECK-NOT:   call void @lowfat_oob_error
; CHECK-LABEL: define void @do_check_me(
; CHECK:       call void @lowfat_oob_error
