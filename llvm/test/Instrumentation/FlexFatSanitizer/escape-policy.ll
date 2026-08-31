; RUN: opt < %s -passes=flexfat -S | FileCheck %s
; RUN: opt < %s -passes='default<O0>,flexfat,verify' -disable-output
; RUN: opt < %s -passes='default<O2>,flexfat,verify' -disable-output
; RUN: opt < %s -passes='default<O3>,flexfat,verify' -disable-output
; RUN: not opt < %s -passes='flexfat<strict-escapes>' -disable-output 2>&1 | FileCheck %s --check-prefix=REMOVED

target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

declare ptr @malloc(i64)
declare void @free(ptr)
declare ptr @realloc(ptr, i64)
declare void @_ZdlPv(ptr)
declare void @custom_free(ptr allocptr, ptr) allockind("free")
declare ptr @custom_realloc(ptr, ptr allocptr, i64) allockind("realloc") allocsize(2)
declare void @sink(ptr)

; Every dynamically checked pointer escape uses an unsigned > class-size
; predicate so an exact one-past value can cross the escape boundary.
define void @call_escape(i64 %n) {
; CHECK-LABEL: @call_escape(
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  call void @sink(ptr %q)
  ret void
}

; Calls that release or replace an allocation require its original base and
; therefore reject an exact one-past consumed operand.
define void @free_escape(i64 %n) {
; CHECK-LABEL: @free_escape(
; CHECK: icmp uge i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  call void @free(ptr %q)
  ret void
}

define void @realloc_escape(i64 %n) {
; CHECK-LABEL: @realloc_escape(
; CHECK: icmp uge i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  %replacement = call ptr @realloc(ptr %q, i64 32)
  ret void
}

define void @delete_escape(i64 %n) {
; CHECK-LABEL: @delete_escape(
; CHECK: icmp uge i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  call void @_ZdlPv(ptr %q)
  ret void
}

; Allocation attributes can place the consumed operand anywhere.  Only that
; operand is strict; unrelated pointer arguments retain ordinary call-escape
; one-past permission.
define void @attribute_free_escape(i64 %consumed_offset, i64 %other_offset) {
; CHECK-LABEL: @attribute_free_escape(
; CHECK: icmp uge i64
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %consumed = getelementptr i8, ptr %p, i64 %consumed_offset
  %other_base = call ptr @malloc(i64 16)
  %other = getelementptr i8, ptr %other_base, i64 %other_offset
  call void @custom_free(ptr %consumed, ptr %other)
  ret void
}

define void @attribute_free_duplicate_operand(i64 %offset) {
; CHECK-LABEL: @attribute_free_duplicate_operand(
; CHECK-COUNT-1: call void @__flexfat_report_oob
; CHECK: call void @custom_free
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %offset
  call void @custom_free(ptr %q, ptr %q)
  ret void
}

define void @attribute_realloc_escape(i64 %consumed_offset, i64 %other_offset) {
; CHECK-LABEL: @attribute_realloc_escape(
; CHECK: icmp uge i64
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %consumed = getelementptr i8, ptr %p, i64 %consumed_offset
  %other_base = call ptr @malloc(i64 16)
  %other = getelementptr i8, ptr %other_base, i64 %other_offset
  %replacement = call ptr @custom_realloc(ptr %other, ptr %consumed, i64 32)
  ret void
}

define ptr @return_escape(i64 %n) {
; CHECK-LABEL: @return_escape(
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  ret ptr %q
}

define void @store_escape(ptr %slot, i64 %n) {
; CHECK-LABEL: @store_escape(
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  store ptr %q, ptr %slot
  ret void
}

define { ptr } @aggregate_escape(i64 %n) {
; CHECK-LABEL: @aggregate_escape(
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  %aggregate = insertvalue { ptr } poison, ptr %q, 0
  ret { ptr } %aggregate
}

define <2 x ptr> @vector_aggregate_escape(i64 %n) {
; CHECK-LABEL: @vector_aggregate_escape(
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  %aggregate = insertelement <2 x ptr> poison, ptr %q, i32 0
  ret <2 x ptr> %aggregate
}

define i64 @integer_escape(i64 %n) {
; CHECK-LABEL: @integer_escape(
; CHECK: icmp ugt i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  %bits = ptrtoint ptr %q to i64
  ret i64 %bits
}

; Scalar point accesses remain strict and reject equality.
define i8 @scalar_load(i64 %n) {
; CHECK-LABEL: @scalar_load(
; CHECK: icmp uge i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  %value = load i8, ptr %q
  ret i8 %value
}

define void @scalar_store(i64 %n) {
; CHECK-LABEL: @scalar_store(
; CHECK: icmp uge i64
  %p = call ptr @malloc(i64 16)
  %q = getelementptr i8, ptr %p, i64 %n
  store i8 0, ptr %q
  ret void
}

; A statically known exact one-past escape is valid and therefore removed.
define void @static_exact_one_past_escape() {
; CHECK-LABEL: @static_exact_one_past_escape(
; CHECK-NOT: __flexfat_report_oob
; CHECK: call void @sink
  %p = call ptr @malloc(i64 16)
  %end = getelementptr i8, ptr %p, i64 16
  call void @sink(ptr %end)
  ret void
}

define void @static_exact_one_past_free() {
; CHECK-LABEL: @static_exact_one_past_free(
; CHECK: icmp uge i64
; CHECK: call void @__flexfat_report_oob
; CHECK: call void @free
  %p = call ptr @malloc(i64 16)
  %end = getelementptr i8, ptr %p, i64 16
  call void @free(ptr %end)
  ret void
}

define void @static_exact_one_past_realloc() {
; CHECK-LABEL: @static_exact_one_past_realloc(
; CHECK: icmp uge i64
; CHECK: call void @__flexfat_report_oob
; CHECK: call ptr @realloc
  %p = call ptr @malloc(i64 16)
  %end = getelementptr i8, ptr %p, i64 16
  %replacement = call ptr @realloc(ptr %end, i64 32)
  ret void
}

; Exact allocation bases are statically valid consumed operands.
define void @allocation_base_free() {
; CHECK-LABEL: @allocation_base_free(
; CHECK-NOT: __flexfat_report_oob
; CHECK: call void @free
  %p = call ptr @malloc(i64 16)
  call void @free(ptr %p)
  ret void
}

define void @allocation_base_realloc() {
; CHECK-LABEL: @allocation_base_realloc(
; CHECK-NOT: __flexfat_report_oob
; CHECK: call ptr @realloc
  %p = call ptr @malloc(i64 16)
  %replacement = call ptr @realloc(ptr %p, i64 32)
  ret void
}

; A statically known exact one-past scalar access remains checked because the
; pointer value is valid but dereferencing it is not.
define i8 @static_exact_one_past_load() {
; CHECK-LABEL: @static_exact_one_past_load(
; CHECK: icmp uge i64
; CHECK: call void @__flexfat_report_oob
; CHECK: load i8
  %p = call ptr @malloc(i64 16)
  %end = getelementptr i8, ptr %p, i64 16
  %value = load i8, ptr %end
  ret i8 %value
}

define void @static_exact_one_past_store() {
; CHECK-LABEL: @static_exact_one_past_store(
; CHECK: icmp uge i64
; CHECK: call void @__flexfat_report_oob
; CHECK: store i8 0
  %p = call ptr @malloc(i64 16)
  %end = getelementptr i8, ptr %p, i64 16
  store i8 0, ptr %end
  ret void
}

; An offset beyond one-past becomes unknown and keeps its dynamic escape check.
define void @static_beyond_one_past_escape() {
; CHECK-LABEL: @static_beyond_one_past_escape(
; CHECK: icmp ugt i64
; CHECK: call void @__flexfat_report_oob
  %p = call ptr @malloc(i64 16)
  %past = getelementptr i8, ptr %p, i64 17
  call void @sink(ptr %past)
  ret void
}

; REMOVED: invalid FlexFatSanitizer pass parameter 'strict-escapes'
