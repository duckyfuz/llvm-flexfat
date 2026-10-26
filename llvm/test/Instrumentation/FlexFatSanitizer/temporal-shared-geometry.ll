; RUN: opt -passes='flexfat<tbi=last-byte>,verify' -S %s | FileCheck %s --check-prefix=CONTAINED
; RUN: opt -passes='flexfat<tbi=last-byte;no-share-contained-geometry>,verify' -S %s | FileCheck %s --check-prefix=SEPARATE
; RUN: opt -passes='flexfat<tbi=last-byte;whole-access>,verify' -S %s | FileCheck %s
; RUN: %if flexfat-custom-config %{ opt -passes='flexfat<tbi=last-byte;whole-access>,verify' -S %s | FileCheck %s --check-prefix=CUSTOM %}
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
; CHECK: [[ENTRY:%flexfat.metadata]] = inttoptr i64 {{.*}} to ptr
; CHECK: load i8, ptr [[ENTRY]]
; CHECK: load volatile i64, ptr %p
; CHECK-NOT: load i64, ptr
; CHECK: load i8, ptr [[ENTRY]]
; CHECK: load volatile i64, ptr %p
; CUSTOM-LABEL: @same_pointer(
; CUSTOM: mul i128
; CUSTOM: %flexfat.slot = sub i64
; CUSTOM: %flexfat.base.int = mul i64 %flexfat.slot,
; CUSTOM-NOT: mul i128
; CUSTOM: load i8
; CUSTOM-NOT: mul i128
; CUSTOM: load i8
; CUSTOM-NOT: mul i128
; CUSTOM: ret i64
  %a = load volatile i64, ptr %p
  %b = load volatile i64, ptr %p
  %sum = add i64 %a, %b
  ret i64 %sum
}

define i8 @derived_pointer(ptr %p, i64 %n) {
; CHECK-LABEL: @derived_pointer(
; CHECK: [[DERIVED:%flexfat.metadata[0-9]*]] = inttoptr i64 {{.*}} to ptr
; CHECK: %q = getelementptr i8, ptr %p, i64 %n
; CHECK: call void @__flexfat_report_oob
; CHECK: load i8, ptr [[DERIVED]]
; CHECK: %a = load volatile i8, ptr %q
; CHECK-NOT: load i64, ptr
; CHECK-NOT: mul i128
; CHECK: load i8, ptr [[DERIVED]]
; CHECK: %b = load volatile i8, ptr %q
; CUSTOM-LABEL: @derived_pointer(
; CUSTOM: mul i128
; CUSTOM: %flexfat.metadata = inttoptr
; CUSTOM: %q = getelementptr i8, ptr %p, i64 %n
; CUSTOM: call void @__flexfat_report_oob
; CUSTOM: load i8
; CUSTOM-NOT: mul i128
; CUSTOM: load i8
; CUSTOM-NOT: mul i128
; CUSTOM: ret i8
  %q = getelementptr i8, ptr %p, i64 %n
  %a = load volatile i8, ptr %q
  %b = load volatile i8, ptr %q
  %sum = add i8 %a, %b
  ret i8 %sum
}

define i8 @marked_derived(ptr %p, i64 %n) {
; CHECK-LABEL: @marked_derived(
; CHECK: %q = getelementptr i8, ptr %p, i64 %n
; CHECK: %flexfat.metadata{{[0-9]*}} = inttoptr
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: load i8
; CHECK: %v = load i8, ptr %q
  %q = getelementptr i8, ptr %p, i64 %n
  %v = load i8, ptr %q, !flexfat.instrumented !0
  ret i8 %v
}

define ptr @escape_only(ptr %p, i64 %offset) {
; CHECK-LABEL: @escape_only(
; CHECK-NOT: flexfat.metadata
; CHECK-NOT: load i8
; CHECK: ret ptr %q
  %q = getelementptr i8, ptr %p, i64 %offset
  ret ptr %q
}

define i8 @constant_address() {
; CHECK-LABEL: @constant_address(
; CHECK: [[CONST_ENTRY:%flexfat.metadata]] = inttoptr i64 {{.*}} to ptr
; CHECK: load i8, ptr [[CONST_ENTRY]]
; CHECK: load volatile i8, ptr inttoptr (i64 17592186044428 to ptr)
; CUSTOM-LABEL: @constant_address(
; CUSTOM: [[CONST_ENTRY:%flexfat.metadata]] = inttoptr i64 {{.*}} to ptr
; CUSTOM: load i8, ptr [[CONST_ENTRY]]
; CUSTOM: load volatile i8, ptr inttoptr (i64 17592186044428 to ptr)
  %v = load volatile i8, ptr inttoptr (i64 17592186044428 to ptr)
  ret i8 %v
}

!0 = !{}

; Premarked accesses lack a local spatial proof, exposing the contained-sharing option.
declare ptr @malloc(i64)

; CONTAINED-LABEL: @contained_marked(
; CONTAINED: %p = call ptr @malloc(i64 64)
; CONTAINED: [[META:%flexfat.metadata[0-9]*]] = inttoptr
; CONTAINED: %q = getelementptr i32, ptr %p, i64 1
; CONTAINED: load i8, ptr [[META]],
; CONTAINED: %a = load volatile i32, ptr %q
; CONTAINED: %r = getelementptr i32, ptr %p, i64 2
; CONTAINED-NOT: = inttoptr
; CONTAINED: load i8, ptr [[META]],
; CONTAINED: %b = load volatile i32, ptr %r
; CONTAINED: ret i32
; SEPARATE-LABEL: @contained_marked(
; SEPARATE: %q = getelementptr i32, ptr %p, i64 1
; SEPARATE: [[FIRST:%flexfat.metadata[0-9]*]] = inttoptr
; SEPARATE: load i8, ptr [[FIRST]],
; SEPARATE: %a = load volatile i32, ptr %q
; SEPARATE: %r = getelementptr i32, ptr %p, i64 2
; SEPARATE: [[SECOND:%flexfat.metadata[0-9]+]] = inttoptr
; SEPARATE: load i8, ptr [[SECOND]],
; SEPARATE: %b = load volatile i32, ptr %r
; SEPARATE: ret i32
define i32 @contained_marked() {
  %p = call ptr @malloc(i64 64)
  %q = getelementptr i32, ptr %p, i64 1
  %a = load volatile i32, ptr %q, !flexfat.instrumented !0
  %r = getelementptr i32, ptr %p, i64 2
  %b = load volatile i32, ptr %r, !flexfat.instrumented !0
  %sum = add i32 %a, %b
  ret i32 %sum
}
