; RUN: opt -passes='flexfat<tbi>,verify' -S %s | FileCheck %s
; RUN: opt -passes='flexfat<tbi>,function(dce),verify' -S %s | FileCheck %s --check-prefix=LIVE
; RUN: opt -passes='flexfat<tbi>,default<O2>,verify' -S %s | FileCheck %s --check-prefix=OPT

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

@reader_alias = alias i32 (ptr), ptr @reader
declare i32 @personality(...)

; The caller deliberately precedes the callee. Keep the callee uninlined so
; dead-call elimination has to respect its updated call-site attributes.
define void @caller(ptr %p) "no-sanitize-flexfat" {
; CHECK-LABEL: define void @caller(
; CHECK: call i32 @reader(ptr %p) #[[CALL:[0-9]+]]
; CHECK: call i32 @reader_alias(ptr %p) #[[CALL]]
; OPT-LABEL: define void @caller(
; OPT: call i32 @reader(ptr %p)
; OPT: call i32 @reader_alias(ptr %p)
; LIVE-LABEL: define void @caller(
; LIVE: call i32 @reader(ptr %p)
; LIVE: call i32 @reader_alias(ptr %p)
  %a = call i32 @reader(ptr %p) #3
  %b = call i32 @reader_alias(ptr %p) #0
  ret void
}

define void @invoker(ptr %p) "no-sanitize-flexfat" personality ptr @personality {
; CHECK-LABEL: define void @invoker(
; CHECK: invoke i32 @reader_alias(ptr %p) #[[INVOKE:[0-9]+]]
  %v = invoke i32 @reader_alias(ptr %p) #1 to label %done unwind label %unwind
 done:
  ret void
 unwind:
  %lp = landingpad { ptr, i32 } cleanup
  resume { ptr, i32 } %lp
}

define i32 @reader(ptr %p) #2 {
; CHECK-LABEL: define i32 @reader(ptr %p)
; CHECK-SAME: #[[READER:[0-9]+]]
; CHECK: call void @__flexfat_check_temporal
; OPT-LABEL: define i32 @reader(
; OPT: call void @__flexfat_check_temporal
; LIVE-LABEL: define i32 @reader(
; LIVE: call void @__flexfat_check_temporal
  %v = load i32, ptr %p
  ret i32 %v
}

; Excluded callees and their calls must retain their original attributes.
define i32 @excluded(ptr %p) #0 "no-sanitize-flexfat" {
; CHECK-LABEL: define i32 @excluded(
; CHECK-NOT: call void @__flexfat_check_temporal
; CHECK: ret i32
  %v = load i32, ptr %p
  ret i32 %v
}
define i32 @excluded_caller(ptr %p) "no-sanitize-flexfat" {
; CHECK-LABEL: define i32 @excluded_caller(
; CHECK: call i32 @excluded(ptr %p) #[[UNCHANGED:[0-9]+]]
  %v = call i32 @excluded(ptr %p) #0
  ret i32 %v
}

; CHECK: attributes #[[READER]] = { noinline nounwind willreturn memory(readwrite) }
; CHECK: attributes #[[CALL]] = { nounwind willreturn memory(readwrite) }
; CHECK: attributes #[[INVOKE]] = { willreturn memory(readwrite) }
; CHECK: attributes #[[UNCHANGED]] = { nofree nosync nounwind willreturn memory(argmem: read) }
attributes #0 = { nofree nosync nounwind willreturn memory(argmem: read) }
attributes #1 = { nofree nosync willreturn memory(argmem: read) }
attributes #2 = { noinline nofree nosync nounwind speculatable willreturn memory(argmem: read) }
attributes #3 = { nofree nosync nounwind speculatable willreturn memory(argmem: read) }
