; REQUIRES: aarch64-registered-target
; RUN: %clang_cc1 -triple aarch64-linux-gnu -x ir -fsanitize=flexfat -fsanitize-flexfat-tbi -O2 -mllvm -print-after=flexfat -emit-llvm -o /dev/null %s 2>&1 | FileCheck %s

; Inspect module setup before scalar-late function instrumentation. Call-site
; invalidation must happen here, even in callers excluded from instrumentation.
; CHECK: IR Dump After {{.*}}FlexFatSanitizerPass
; CHECK-LABEL: define void @caller(
; CHECK: call i32 @reader(ptr %p) #[[CALL:[0-9]+]]
; CHECK-LABEL: define i32 @reader(ptr %p)
; CHECK-SAME: #[[READER:[0-9]+]]
; CHECK-NOT: call void @__flexfat_check_temporal
; CHECK: ret i32 %v
; CHECK: attributes #[[READER]] = { noinline nounwind willreturn memory(readwrite) }
; CHECK: attributes #[[CALL]] = { nounwind willreturn memory(readwrite) }

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

define void @caller(ptr %p) "no-sanitize-flexfat" {
  %v = call i32 @reader(ptr %p) #0
  ret void
}

define i32 @reader(ptr %p) #1 {
  %v = load i32, ptr %p
  ret i32 %v
}

attributes #0 = { nofree nosync nounwind willreturn memory(argmem: read) }
attributes #1 = { noinline nofree nosync nounwind willreturn memory(argmem: read) }
