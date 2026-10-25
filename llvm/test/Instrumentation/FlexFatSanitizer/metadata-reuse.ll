; RUN: opt < %s -mtriple=aarch64-linux-gnu -passes='flexfat<tbi=last-byte>,flexfat<tbi=last-byte>,verify' -disable-output
; RUN: opt < %s -passes='flexfat,verify' -S | FileCheck %s
; RUN: %if flexfat-custom-config %{ opt < %s -passes='flexfat,verify' -S | FileCheck %s --check-prefix=CUSTOM %}

target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"

declare i32 @__gxx_personality_v0(...)

define i8 @argument_gep_chain(ptr %p, i64 %a, i64 %b) {
; CHECK-LABEL: @argument_gep_chain(
; CHECK-COUNT-1: %flexfat.root.int = ptrtoint ptr %p to i64
; CHECK: %flexfat.region.raw = lshr i64 %flexfat.root.int, {{32|38}}
; CHECK: %flexfat.address.valid = icmp ult i64 %flexfat.root.int, 281474976710656
; CHECK: %flexfat.region = select i1 %flexfat.address.valid, i64 %flexfat.region.raw, i64 0
; CHECK-NOT: 17592186044416
; CHECK: %[[SIZE:.*]] = load i64, ptr
; CHECK: %flexfat.base = inttoptr
; CHECK: %q1 = getelementptr i8, ptr %p, i64 %a
; CHECK-NOT: load i64
; CHECK: %[[BOUND1:.*]] = sub i64 %[[SIZE]], 1
; CHECK: icmp uge i64 {{.*}}, %[[BOUND1]]
; CHECK: %q2 = getelementptr i8, ptr %q1, i64 %b
; CHECK-NOT: load i64
; CHECK: %[[BOUND2:.*]] = sub i64 %[[SIZE]], 1
; CHECK: icmp uge i64 {{.*}}, %[[BOUND2]]
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %p to i64
; CUSTOM-LABEL: @argument_gep_chain(
; CUSTOM: %[[MUL128:.*]] = mul i128
; CUSTOM: %[[IDX:.*]] = trunc i128 {{.*}} to i64
; CUSTOM: %[[CANDIDATE:.*]] = mul i64 %[[IDX]], %[[SIZE:.*]]
; CUSTOM-NOT: mul i64
; CUSTOM: %[[TOO_HIGH:.*]] = icmp ugt i64 %[[CANDIDATE]], %flexfat.root.int
; CUSTOM: %[[ADJUST:.*]] = zext i1 %[[TOO_HIGH]] to i64
; CUSTOM: %[[SLOT:.*]] = sub i64 %[[IDX]], %[[ADJUST]]
; CUSTOM: %flexfat.base.int = mul i64 %[[SLOT]], %[[SIZE]]
  %q1 = getelementptr i8, ptr %p, i64 %a
  %x = load i8, ptr %q1
  %q2 = getelementptr i8, ptr %q1, i64 %b
  %y = load i8, ptr %q2
  %sum = add i8 %x, %y
  ret i8 %sum
}

define i8 @loaded_root(ptr %slot) {
; CHECK-LABEL: @loaded_root(
; CHECK: %p = load ptr, ptr %slot
; CHECK-NEXT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %p to i64
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %p to i64
  %p = load ptr, ptr %slot
  %x = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %y = load i8, ptr %q
  %sum = add i8 %x, %y
  ret i8 %sum
}

define i8 @selected_root(i1 %choose, ptr %a, ptr %b) {
; CHECK-LABEL: @selected_root(
; CHECK: %p = select i1 %choose, ptr %a, ptr %b
; CHECK-NEXT: %flexfat.root.int = ptrtoint ptr %p to i64, !nosanitize
; CHECK-NEXT: %flexfat.region.raw = lshr i64 %flexfat.root.int, {{32|38}}, !nosanitize
; CHECK-NEXT: %flexfat.address.valid = icmp ult i64 %flexfat.root.int, 281474976710656, !nosanitize
; CHECK-NEXT: %flexfat.region = select i1 %flexfat.address.valid, i64 %flexfat.region.raw, i64 0, !nosanitize
; CHECK-NOT: ptrtoint ptr %a
; CHECK-NOT: ptrtoint ptr %b
  %p = select i1 %choose, ptr %a, ptr %b
  %x = load i8, ptr %p
  %q = getelementptr i8, ptr %p, i64 1
  %y = load i8, ptr %q
  %sum = add i8 %x, %y
  ret i8 %sum
}

define i8 @selected_shared_root(i1 %choose, ptr %root, i64 %a, i64 %b) {
; CHECK-LABEL: @selected_shared_root(
; CHECK-COUNT-1: %flexfat.root.int = ptrtoint ptr %root to i64
; CHECK: %left = getelementptr i8, ptr %root, i64 %a
; CHECK: %right.raw = getelementptr i8, ptr %root, i64 %b
; CHECK: %right = freeze ptr %right.raw
; CHECK: %p = select i1 %choose, ptr %left, ptr %right
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %p
  %left = getelementptr i8, ptr %root, i64 %a
  %right.raw = getelementptr i8, ptr %root, i64 %b
  %right = freeze ptr %right.raw
  %p = select i1 %choose, ptr %left, ptr %right
  %v = load i8, ptr %p
  ret i8 %v
}

; An inactive short-string union arm may contain arbitrary high bits.
define i8 @cpar_union_select(i1 %is_long, ptr %long_data, i64 %short_bits) {
; CHECK-LABEL: @cpar_union_select(
; CHECK: %inactive = inttoptr i64 %tagged to ptr
; CHECK-NEXT: %selected = select i1 %is_long, ptr %long_data, ptr %inactive
; CHECK-NEXT: %flexfat.root.int = ptrtoint ptr %selected to i64, !nosanitize
; CHECK-NOT: ptrtoint ptr %inactive
; CHECK: %flexfat.address.valid = icmp ult i64 %flexfat.root.int, 281474976710656
; CHECK: select i1 %flexfat.address.valid, i64 %flexfat.region.raw, i64 0
  %tagged = or i64 %short_bits, -281474976710656
  %inactive = inttoptr i64 %tagged to ptr
  %selected = select i1 %is_long, ptr %long_data, ptr %inactive
  %v = load i8, ptr %selected
  ret i8 %v
}

define i8 @phi_root(ptr %seed, i1 %again) {
; CHECK-LABEL: @phi_root(
; CHECK-COUNT-1: %flexfat.root.int = ptrtoint ptr %seed to i64
; CHECK: %flexfat.base = phi ptr [ %flexfat.base{{[0-9]+}}, %entry ], [ %flexfat.base, %{{[0-9A-Za-z_.]+}} ]
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %seed to i64
entry:
  br label %loop
loop:
  %p = phi ptr [ %seed, %entry ], [ %next, %loop ]
  %x = load i8, ptr %p
  %next = getelementptr i8, ptr %p, i64 1
  br i1 %again, label %loop, label %exit
exit:
  ret i8 %x
}

declare ptr @unknown_pointer()

; Splitting the invoke edge must see complete companion-PHI incoming lists.
define i8 @invoke_phi_root(i1 %choose, ptr %fallback)
    personality ptr @__gxx_personality_v0 {
; CHECK-LABEL: @invoke_phi_root(
; CHECK: %p = invoke ptr @unknown_pointer()
; CHECK: to label %{{[0-9A-Za-z_.]+}} unwind label %exception
; CHECK: %root = phi ptr
; CHECK-NEXT: %flexfat.base = phi ptr
; CHECK: %v = load i8, ptr %root
entry:
  br i1 %choose, label %invoke.block, label %other

invoke.block:
  %p = invoke ptr @unknown_pointer() to label %merge unwind label %exception

other:
  br label %merge

merge:
  %root = phi ptr [ %p, %invoke.block ], [ %fallback, %other ]
  %v = load i8, ptr %root
  ret i8 %v

exception:
  %landing = landingpad { ptr, i32 } cleanup
  ret i8 0
}
