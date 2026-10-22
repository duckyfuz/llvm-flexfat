; RUN: opt -passes='flexfat,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<whole-access>,verify' -S %s | FileCheck %s
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi;tbi-storage=shadow>,verify' -S %s | FileCheck %s %}
; RUN: opt -passes='flexfat<tbi;tbi-storage=last-byte>,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<tbi;tbi-storage=prior-byte>,verify' -S %s | FileCheck %s
target triple = "aarch64-unknown-linux-gnu"

declare ptr @malloc(i64)
declare void @sink(ptr)

; A statically known one-past dereference must retain the dynamic check,
; including for a one-byte whole access which used to be elided in shadow mode.
define i8 @one_past_read() {
; CHECK-LABEL: define i8 @one_past_read(
; CHECK: %end = getelementptr i8
; CHECK: %[[BOUND:.*]] = sub i64 {{.*}}, 1
; CHECK: call void @__flexfat_report_oob({{.*}}i64 %[[BOUND]], i8 0)
; CHECK: load volatile i8
  %p = call ptr @malloc(i64 15)
  %end = getelementptr i8, ptr %p, i64 15
  %v = load volatile i8, ptr %end
  ret i8 %v
}

; Legal requested-object one-past escapes remain statically valid.
define void @one_past_escape() {
; CHECK-LABEL: define void @one_past_escape(
; CHECK-NOT: __flexfat_report_oob
; CHECK: call void @sink
  %p = call ptr @malloc(i64 15)
  %end = getelementptr i8, ptr %p, i64 15
  call void @sink(ptr %end)
  ret void
}

; Scalable accesses must also exclude the trailing byte in every mode.
define <vscale x 4 x i32> @scalable_read(ptr %p) {
; CHECK-LABEL: define <vscale x 4 x i32> @scalable_read(
; CHECK: call i64 @llvm.vscale.i64()
; CHECK: %[[VECTORBOUND:.*]] = sub i64 {{.*}}, 1
; CHECK: call void @__flexfat_report_oob({{.*}}i64 %[[VECTORBOUND]], i8 0)
; CHECK: load volatile <vscale x 4 x i32>
  %v = load volatile <vscale x 4 x i32>, ptr %p
  ret <vscale x 4 x i32> %v
}
