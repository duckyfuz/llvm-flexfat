; RUN: opt < %s -passes=flexfat -S | FileCheck %s
; RUN: opt < %s -passes='flexfat<whole-access>' -S | FileCheck %s --check-prefix=WHOLE
target datalayout = "e-m:o-i64:64-i128:128-n32:64-S128-Fn32"

; A bare input pointer at offset zero is statically elided like LowFat.  Use a
; derived pointer with an unknown static bound to exercise emitted checks.
define i32 @test_load(ptr %p) {
; CHECK-LABEL: @test_load
; CHECK: %flexfat.root.int = ptrtoint ptr %p to i64
; CHECK: %flexfat.region.raw = lshr i64 %flexfat.root.int, {{32|38}}
; CHECK: %flexfat.address.valid = icmp ult i64 %flexfat.root.int, 281474976710656
; CHECK: %flexfat.region = select i1 %flexfat.address.valid, i64 %flexfat.region.raw, i64 0
; CHECK-NOT: sub i64 {{.*}}, 17592186044416
; CHECK: %flexfat.base = inttoptr
; CHECK: %[[PTR_INT:.*]] = ptrtoint ptr %q to i64
; CHECK: getelementptr inbounds i64, ptr {{.*}}, i64 %flexfat.region
; CHECK: load i64
; CHECK: icmp uge i64
; CHECK: call void @__flexfat_report_oob
; WHOLE-LABEL: @test_load
; WHOLE: icmp ugt i64 4, {{.*}}
; WHOLE: call void @__flexfat_report_oob
; CHECK: %val = load i32, ptr %q
  %q = getelementptr i8, ptr %p, i64 1
  %val = load i32, ptr %q, align 4
  ret i32 %val
}

; Test 2: Store should be instrumented
define void @test_store(ptr %p, i32 %v) {
; CHECK-LABEL: @test_store
; CHECK: ptrtoint ptr %q to i64
; CHECK: call void @__flexfat_report_oob
; CHECK: store i32 %v, ptr %q
  %q = getelementptr i8, ptr %p, i64 1
  store i32 %v, ptr %q, align 4
  ret void
}

; Test 3: Volatile accesses are real dereferences and must be instrumented.
define i32 @test_volatile_load(ptr %p) {
; CHECK-LABEL: @test_volatile_load
; CHECK: call void @__flexfat_report_oob
; CHECK: load volatile i32, ptr %q
  %q = getelementptr i8, ptr %p, i64 1
  %val = load volatile i32, ptr %q, align 4
  ret i32 %val
}

define void @test_volatile_store(ptr %p, i32 %v) {
; CHECK-LABEL: @test_volatile_store
; CHECK: call void @__flexfat_report_oob
; CHECK: store volatile i32 %v, ptr %q
  %q = getelementptr i8, ptr %p, i64 1
  store volatile i32 %v, ptr %q, align 4
  ret void
}

; Test 4: Runtime functions (__flexfat_*) should NOT be instrumented
define void @__flexfat_internal_test(ptr %p) {
; CHECK-LABEL: @__flexfat_internal_test
; CHECK-NOT: call void @__flexfat_report_oob
  store i32 0, ptr %p
  ret void
}

; Test 5: complete-width checking remains available only through the explicit
; whole-access option.
define i8 @test_load_i8(ptr %p) {
; CHECK-LABEL: @test_load_i8
; CHECK: %[[PTR_INT:.*]] = ptrtoint ptr %q to i64
; CHECK: sub i64 %[[PTR_INT]], {{.*}}
; CHECK: icmp uge i64
; CHECK: call void @__flexfat_report_oob
; WHOLE-LABEL: @test_load_i8
; WHOLE: icmp ugt i64 1, {{.*}}
  %q = getelementptr i8, ptr %p, i64 1
  %val = load i8, ptr %q, align 1
  ret i8 %val
}

; Test 6: whole-access mode extends an i64 store through all eight bytes.
define void @test_store_i64(ptr %p, i64 %v) {
; CHECK-LABEL: @test_store_i64
; CHECK: %[[PTR_INT:.*]] = ptrtoint ptr %q to i64
; CHECK: sub i64 %[[PTR_INT]], {{.*}}
; CHECK: icmp uge i64
; CHECK: call void @__flexfat_report_oob
; WHOLE-LABEL: @test_store_i64
; WHOLE: icmp ugt i64 8, {{.*}}
  %q = getelementptr i8, ptr %p, i64 1
  store i64 %v, ptr %q, align 8
  ret void
}

; Test 7: AtomicRMW should be instrumented
define i32 @test_atomic_rmw(ptr %p) {
; CHECK-LABEL: @test_atomic_rmw
; CHECK: call void @__flexfat_report_oob
; CHECK: atomicrmw add ptr %q, i32 1
  %q = getelementptr i8, ptr %p, i64 1
  %old = atomicrmw add ptr %q, i32 1 monotonic
  ret i32 %old
}

; Test 8: AtomicCmpXchg should be instrumented
define { i32, i1 } @test_atomic_cmpxchg(ptr %p) {
; CHECK-LABEL: @test_atomic_cmpxchg
; CHECK: call void @__flexfat_report_oob
; CHECK: cmpxchg ptr %q, i32 0, i32 1
  %q = getelementptr i8, ptr %p, i64 1
  %val = cmpxchg ptr %q, i32 0, i32 1 monotonic monotonic
  ret { i32, i1 } %val
}

; Test 9: Multiple accesses in one function
define void @test_multiple(ptr %p, ptr %q) {
; CHECK-LABEL: @test_multiple
; CHECK: call void @__flexfat_report_oob
; CHECK: load i32
; CHECK: call void @__flexfat_report_oob
; CHECK: store i32
  %p.derived = getelementptr i8, ptr %p, i64 1
  %q.derived = getelementptr i8, ptr %q, i64 1
  %val = load i32, ptr %p.derived, align 4
  store i32 %val, ptr %q.derived, align 4
  ret void
}

; Test 10: nosanitize suppresses instrumentation.
define i32 @test_nosanitize(ptr %p) {
; CHECK-LABEL: @test_nosanitize
; CHECK-NOT: call void @__flexfat_report_oob
; CHECK: %val = load i32, ptr %p
  %val = load i32, ptr %p, align 4, !nosanitize !0
  ret i32 %val
}

!0 = !{}
