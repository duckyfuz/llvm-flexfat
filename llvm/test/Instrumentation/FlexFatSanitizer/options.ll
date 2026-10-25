; RUN: opt -passes='flexfat<tbi=last-byte;tbi=off;whole-access;no-whole-access>' -S %s | FileCheck %s --check-prefix=OFF
; RUN: opt -passes='flexfat<tbi=last-byte;no-share-contained-geometry;tbi-ordinary-loads>,verify' -S %s | FileCheck %s --check-prefix=ORDINARY
; RUN: opt -passes='flexfat<tbi=last-byte;tbi-ordinary-loads;no-tbi-ordinary-loads>,verify' -S %s | FileCheck %s --check-prefix=ATOMIC
; RUN: not opt -passes='flexfat<tbi>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<tbi-storage=last-byte>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<tbi=invalid>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REMOVED
; RUN: not opt -passes='flexfat<no-share-contained-geometry>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=ACTIVE
; RUN: not opt -passes='flexfat<tbi=shadow;tbi-ordinary-loads>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=IN-SLOT
; OFF-NOT: __flexfat_report_temporal
; ORDINARY: load i8, ptr
; ORDINARY-NOT: load atomic
; ORDINARY: call void @__flexfat_report_temporal_v3
; ATOMIC: load atomic i8, ptr
; REMOVED: invalid FlexFatSanitizer pass parameter
; ACTIVE: FlexFat temporal options require active TBI
; IN-SLOT: FlexFat ordinary generation loads require last-byte or prior-byte TBI

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

define i32 @read(ptr %p) {
  %v = load volatile i32, ptr %p
  ret i32 %v
}
