; RUN: opt -passes='flexfat<tbi;whole-access>,verify' -S %s | FileCheck %s
; RUN: %if flexfat-custom-config %{ opt -passes='flexfat<tbi;whole-access>,verify' -S %s | FileCheck %s --check-prefix=CUSTOM %}
; RUN: opt -mtriple=x86_64-linux-gnu -passes='flexfat<whole-access>,verify' -S %s | FileCheck %s --check-prefix=SPATIAL
; RUN: opt -passes='flexfat<whole-access>,verify' -S %s | FileCheck %s --check-prefix=SPATIAL

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"
; SPATIAL-NOT: 72057594037927935
; SPATIAL-NOT: flexfat.metadata
; SPATIAL-NOT: @__flexfat_report_temporal
; SPATIAL-NOT: tbi_abi
; SPATIAL: define i64 @same_pointer
; SPATIAL-NOT: 72057594037927935
; SPATIAL-NOT: flexfat.metadata
; SPATIAL-NOT: @__flexfat_report_temporal
; SPATIAL-NOT: tbi_abi

define i64 @same_pointer(ptr %p) {
; CHECK-LABEL: @same_pointer(
; CHECK: [[RAW:%.*]] = and i64 {{.*}}, 72057594037927935
; CHECK: [[ENTRY:%.*]] = inttoptr i64 %flexfat.metadata.address to ptr
; CHECK: load atomic i8, ptr [[ENTRY]] acquire
; CHECK: load volatile i64, ptr %p
; CHECK-NOT: load i64, ptr
; CHECK-NOT: and i64 {{.*}}, 72057594037927935
; CHECK: load atomic i8, ptr [[ENTRY]] acquire
; CHECK: load volatile i64, ptr %p
; CUSTOM-LABEL: @same_pointer(
; CUSTOM: mul i128
; CUSTOM: %flexfat.slot = sub i64
; CUSTOM: %flexfat.base.int = mul i64 %flexfat.slot,
; CUSTOM-NOT: mul i128
; CUSTOM: load atomic i8
; CUSTOM-NOT: mul i128
; CUSTOM: load atomic i8
; CUSTOM-NOT: mul i128
; CUSTOM: ret i64
  %a = load volatile i64, ptr %p
  %b = load volatile i64, ptr %p
  %sum = add i64 %a, %b
  ret i64 %sum
}

; An arbitrary GEP may cross a slot, so it needs its own current-address
; geometry. Both uses share that geometry while retaining separate acquires.
define i8 @derived_pointer(ptr %p, i64 %n) {
; CHECK-LABEL: @derived_pointer(
; CHECK: %q = getelementptr i8, ptr %p, i64 %n
; CHECK: [[DERIVED:%.*]] = inttoptr i64 %flexfat.metadata.address{{[0-9]*}} to ptr
; CHECK: load atomic i8, ptr [[DERIVED]] acquire
; CHECK: %a = load volatile i8, ptr %q
; CHECK-NOT: load i64, ptr
; CHECK-NOT: mul i128
; CHECK: load atomic i8, ptr [[DERIVED]] acquire
; CHECK: %b = load volatile i8, ptr %q
; CUSTOM-LABEL: @derived_pointer(
; CUSTOM: mul i128
; CUSTOM: %q = getelementptr i8, ptr %p, i64 %n
; CUSTOM: mul i128
; CUSTOM: load atomic i8
; CUSTOM-NOT: mul i128
; CUSTOM: load atomic i8
; CUSTOM-NOT: mul i128
; CUSTOM: ret i8
  %q = getelementptr i8, ptr %p, i64 %n
  %a = load volatile i8, ptr %q
  %b = load volatile i8, ptr %q
  %sum = add i8 %a, %b
  ret i8 %sum
}

; Spatial-only uses in a TBI module do not request temporal-only fields.
define ptr @escape_only(ptr %p, i64 %offset) {
; CHECK-LABEL: @escape_only(
; CHECK-NOT: flexfat.metadata
; CHECK-NOT: flexfat.managed
; CHECK-NOT: load atomic
; CHECK: ret ptr %q
  %q = getelementptr i8, ptr %p, i64 %offset
  ret ptr %q
}
