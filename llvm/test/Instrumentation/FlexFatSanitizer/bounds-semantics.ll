; RUN: split-file %s %t
; RUN: opt < %t/main.ll -passes=flexfat -S | FileCheck %s
; RUN: opt < %t/main.ll -passes='default<O2>,flexfat,verify' -disable-output
; RUN: opt < %t/main.ll -passes='default<O3>,flexfat,verify' -disable-output
; RUN: opt < %t/main.ll -passes='flexfat,flexfat,verify' -disable-output
; RUN: opt < %t/helper-only.ll -passes=flexfat -S | FileCheck %s --check-prefix=HELPER
; RUN: opt < %t/helper-only.ll -passes='flexfat,flexfat' -S | FileCheck %s --check-prefix=IDEMPOTENT
; RUN: opt < %t/helper-only.ll -passes='function(require<domtree>),flexfat' -disable-output -debug-pass-manager 2>&1 | FileCheck %s --check-prefix=INVALIDATE
;--- main.ll
target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"

declare void @sink(ptr)
declare ptr @allocate()
declare ptr @malloc(i64)
declare void @llvm.memcpy.p0.p0.i64(ptr nocapture writeonly, ptr nocapture readonly, i64, i1 immarg)
declare i32 @__gxx_personality_v0(...)

; Forming and comparing a one-past pointer is legal and must not be checked.
define i1 @legal_one_past(ptr %p) {
; CHECK-LABEL: @legal_one_past(
; CHECK-NOT: __flexfat_report_oob
; CHECK: %end = getelementptr i8, ptr %p, i64 16
; CHECK: ret i1
  %end = getelementptr i8, ptr %p, i64 16
  %same = icmp eq ptr %end, %p
  ret i1 %same
}

; Integer conversion for an internal pointer comparison is not an escape.
define i1 @legal_one_past_integer_comparison(ptr %p) {
; CHECK-LABEL: @legal_one_past_integer_comparison(
; CHECK-NOT: __flexfat_report_oob
; CHECK: %end = getelementptr i8, ptr %p, i64 16
; CHECK: %end.int = ptrtoint ptr %end to i64
; CHECK: ret i1
  %end = getelementptr i8, ptr %p, i64 16
  %end.int = ptrtoint ptr %end to i64
  %base.int = ptrtoint ptr %p to i64
  %after = icmp uge i64 %end.int, %base.int
  ret i1 %after
}

; A dereference is checked against the allocation from which the GEP arose,
; not against a base rediscovered from the derived pointer.
define i8 @derived_dereference(ptr %p, i64 %n) {
; CHECK-LABEL: @derived_dereference(
; CHECK: %q = getelementptr i8, ptr %p, i64 %n
; CHECK: ptrtoint ptr %q to i64
; CHECK: ptrtoint ptr %p to i64
; CHECK: sub i64 {{.*}}, 17592186044416
; CHECK: lshr i64 {{.*}}, {{32|38}}
; CHECK: icmp ult i64
; CHECK-NOT: load i64
; CHECK: br i1 {{.*}}, label %[[VALID:[0-9A-Za-z_.]+]], label %[[CONT:[0-9A-Za-z_.]+]]
; CHECK: [[VALID]]:
; CHECK: getelementptr inbounds i64, ptr inttoptr
; CHECK: load i64
; CHECK: call void @__flexfat_report_oob
; CHECK: [[CONT]]:
; CHECK: load i8, ptr %q
  %q = getelementptr i8, ptr %p, i64 %n
  %v = load i8, ptr %q
  ret i8 %v
}

; Passing a derived pointer to an unknown callee is a genuine escape.
define void @derived_escape(ptr %p, i64 %n) {
; CHECK-LABEL: @derived_escape(
; CHECK: %q = getelementptr i8, ptr %p, i64 %n
; CHECK: ptrtoint ptr %q to i64
; CHECK: ptrtoint ptr %p to i64
; CHECK: call void @__flexfat_report_oob
; CHECK: call void @sink(ptr %q)
  %q = getelementptr i8, ptr %p, i64 %n
  call void @sink(ptr %q)
  ret void
}

; A non-FlexFat pointer is classified only by the region guard.  In
; particular, no fixed table load can execute before that guard.
define i8 @non_flexfat_pointer() {
; CHECK-LABEL: @non_flexfat_pointer(
; CHECK: icmp ult i64
; CHECK-NOT: load i64
; CHECK: br i1 {{.*}}, label %[[VALID:[0-9A-Za-z_.]+]], label %[[CONT:[0-9A-Za-z_.]+]]
; CHECK: [[VALID]]:
; CHECK: load i64
; CHECK: [[CONT]]:
; CHECK: load i8, ptr inttoptr (i64 4096 to ptr)
  %v = load i8, ptr inttoptr (i64 4096 to ptr)
  ret i8 %v
}

; A statically zero-length intrinsic requires no classification or check, even
; when both pointers are null.
define void @zero_memcpy() {
; CHECK-LABEL: @zero_memcpy(
; CHECK-NOT: inttoptr i64 19241453486080
; CHECK-NOT: __flexfat_report_oob
; CHECK: call void @llvm.memcpy.p0.p0.i64(ptr null, ptr null, i64 0, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr null, ptr null, i64 0, i1 false)
  ret void
}

; Dynamic ranges test length!=0 together with region validity before the
; metadata-table block.
define void @dynamic_memcpy(ptr %dst, ptr %src, i64 %len) {
; CHECK-LABEL: @dynamic_memcpy(
; CHECK: icmp ne i64 %len, 0
; CHECK: icmp ult i64
; CHECK: and i1
; CHECK-NOT: load i64
; CHECK: br i1 {{.*}}, label %[[VALID:[0-9A-Za-z_.]+]], label
; CHECK: [[VALID]]:
; CHECK: load i64
  call void @llvm.memcpy.p0.p0.i64(ptr %dst, ptr %src, i64 %len, i1 false)
  ret void
}

; Origin propagation through selects and loop-carried PHIs must remain valid
; SSA while retaining the selected allocation base.
define i8 @selected_origin(i1 %choose, ptr %a, ptr %b, i64 %n) {
; CHECK-LABEL: @selected_origin(
; CHECK: %flexfat.base = select i1 %choose, ptr %a, ptr %b
; CHECK: ptrtoint ptr %flexfat.base to i64
  %p = select i1 %choose, ptr %a, ptr %b
  %q = getelementptr i8, ptr %p, i64 %n
  %v = load i8, ptr %q
  ret i8 %v
}

define i8 @loop_origin(ptr %a, i1 %again) {
; CHECK-LABEL: @loop_origin(
; CHECK: %flexfat.base = phi ptr [ %a, %entry ], [ %flexfat.base, %{{[0-9A-Za-z_.]+}} ]
entry:
  br label %loop
loop:
  %p = phi ptr [ %a, %entry ], [ %next, %loop ]
  %v = load i8, ptr %p
  %next = getelementptr i8, ptr %p, i64 1
  br i1 %again, label %loop, label %exit
exit:
  ret i8 %v
}

; Reloads always recover their own companion base.  They never reconstruct
; provenance from a dominating or syntactically unique store.
define i8 @conditional_spill(i1 %take_store) {
; CHECK-LABEL: @conditional_spill(
; CHECK: %loaded = load ptr, ptr %slot
; CHECK: ptrtoint ptr %loaded to i64
; CHECK: %value = load i8, ptr %loaded
entry:
  %slot = alloca ptr
  br i1 %take_store, label %store, label %merge
store:
  %allocated = call ptr @allocate()
  store ptr %allocated, ptr %slot
  br label %merge
merge:
  %loaded = load ptr, ptr %slot
  %value = load i8, ptr %loaded
  ret i8 %value
}

; Unknown call results, inttoptr values, and aggregate extraction are input
; pointers whose bases are dynamically recovered from the values themselves.
define i8 @unknown_call_result() {
; CHECK-LABEL: @unknown_call_result(
; CHECK: %p = call ptr @allocate()
; CHECK: ptrtoint ptr %p to i64
; CHECK: %v = load i8, ptr %p
  %p = call ptr @allocate()
  %v = load i8, ptr %p
  ret i8 %v
}

define i8 @integer_input(i64 %bits) {
; CHECK-LABEL: @integer_input(
; CHECK: %p = inttoptr i64 %bits to ptr
; CHECK: ptrtoint ptr %p to i64
; CHECK: %v = load i8, ptr %p
  %p = inttoptr i64 %bits to ptr
  %v = load i8, ptr %p
  ret i8 %v
}

define i8 @aggregate_input({ ptr } %aggregate) {
; CHECK-LABEL: @aggregate_input(
; CHECK: %p = extractvalue { ptr } %aggregate, 0
; CHECK: ptrtoint ptr %p to i64
; CHECK: %v = load i8, ptr %p
  %p = extractvalue { ptr } %aggregate, 0
  %v = load i8, ptr %p
  ret i8 %v
}

; Even an unconditional, unique spill is dynamically reconstructed from the
; loaded value, while the pointer store itself is checked as an escape.
define i8 @unconditional_spill() {
; CHECK-LABEL: @unconditional_spill(
; CHECK: %allocated = call ptr @malloc(i64 16)
; CHECK: store ptr %allocated, ptr %slot
; CHECK: %loaded = load ptr, ptr %slot
; CHECK: ptrtoint ptr %loaded to i64
; CHECK: %value = load i8, ptr %loaded
entry:
  %slot = alloca ptr
  %allocated = call ptr @malloc(i64 16)
  store ptr %allocated, ptr %slot
  %loaded = load ptr, ptr %slot
  %value = load i8, ptr %loaded
  ret i8 %value
}

; Integer-only comparisons do not escape, but an integer returned from a
; ptrtoint conversion does.
define i64 @escaping_ptrtoint(ptr %p, i64 %n) {
; CHECK-LABEL: @escaping_ptrtoint(
; CHECK: %q = getelementptr i8, ptr %p, i64 %n
; CHECK: call void @__flexfat_report_oob
; CHECK: %bits = ptrtoint ptr %q to i64
  %q = getelementptr i8, ptr %p, i64 %n
  %bits = ptrtoint ptr %q to i64
  ret i64 %bits
}

@global_byte = global i8 0

; Heap-only mode statically excludes stack and global objects and their GEPs.
define i8 @stack_and_global_are_non_flexfat() {
; CHECK-LABEL: @stack_and_global_are_non_flexfat(
; CHECK-NOT: __flexfat_report_oob
; CHECK: load i8, ptr %stack.gep
; CHECK: load i8, ptr @global_byte
  %stack = alloca [4 x i8]
  %stack.gep = getelementptr [4 x i8], ptr %stack, i64 0, i64 1
  %a = load i8, ptr %stack.gep
  %b = load i8, ptr @global_byte
  %sum = add i8 %a, %b
  ret i8 %sum
}

; Mixed managed/foreign selects mirror the original condition rather than
; guessing that both inputs have the same provenance.
define i8 @mixed_select(i1 %choose) {
; CHECK-LABEL: @mixed_select(
; CHECK: %heap = call ptr @malloc(i64 16)
; CHECK: %flexfat.base = select i1 %choose, ptr %heap, ptr null
; CHECK-NOT: ptrtoint
; CHECK: %v = load i8, ptr %p
  %heap = call ptr @malloc(i64 16)
  %p = select i1 %choose, ptr %heap, ptr @global_byte
  %v = load i8, ptr %p
  ret i8 %v
}

; Nested loop PHIs retain one companion PHI per checked pointer and preserve
; the corresponding predecessor ordering through self-referential edges.
define i8 @nested_phi(ptr %seed, i1 %outer_again, i1 %inner_again) {
; CHECK-LABEL: @nested_phi(
; CHECK: %flexfat.base1 = phi ptr [ %seed, %entry ], [ %flexfat.base, %outer.latch ]
; CHECK: %flexfat.base = phi ptr [ %flexfat.base1, %outer ], [ %flexfat.base, %{{[0-9A-Za-z_.]+}} ]
entry:
  br label %outer
outer:
  %outer.p = phi ptr [ %seed, %entry ], [ %outer.next, %outer.latch ]
  br label %inner
inner:
  %inner.p = phi ptr [ %outer.p, %outer ], [ %inner.next, %inner ]
  %v = load i8, ptr %inner.p
  %inner.next = getelementptr i8, ptr %inner.p, i64 1
  br i1 %inner_again, label %inner, label %outer.latch
outer.latch:
  %outer.next = getelementptr i8, ptr %inner.p, i64 1
  br i1 %outer_again, label %outer, label %exit
exit:
  ret i8 %v
}

define i8 @freeze_origin(ptr %p) {
; CHECK-LABEL: @freeze_origin(
; CHECK: %frozen = freeze ptr %p
; CHECK: ptrtoint ptr %p to i64
; CHECK: %v = load i8, ptr %q
  %frozen = freeze ptr %p
  %q = getelementptr i8, ptr %frozen, i64 1
  %v = load i8, ptr %q
  ret i8 %v
}

define i8 @address_space_origin(ptr %p) {
; CHECK-LABEL: @address_space_origin(
; CHECK: %as = addrspacecast ptr %p to ptr addrspace(1)
; CHECK: %flexfat.base.cast = addrspacecast ptr %p to ptr addrspace(1)
; CHECK: ptrtoint ptr addrspace(1) %as to i64
; CHECK: ptrtoint ptr addrspace(1) %flexfat.base.cast to i64
; CHECK: %v = load i8, ptr addrspace(1) %as
  %as = addrspacecast ptr %p to ptr addrspace(1)
  %v = load i8, ptr addrspace(1) %as
  ret i8 %v
}

; Allocation-family invoke results are their own companion base on the normal
; edge, while the unwind edge remains valid SSA.
define i8 @invoke_allocation() personality ptr @__gxx_personality_v0 {
; CHECK-LABEL: @invoke_allocation(
; CHECK: %p = invoke ptr @malloc(i64 16)
; CHECK-NOT: ptrtoint
; CHECK: %v = load i8, ptr %p
entry:
  %p = invoke ptr @malloc(i64 16) to label %normal unwind label %exception
normal:
  %v = load i8, ptr %p
  ret i8 %v
exception:
  %landing = landingpad { ptr, i32 } cleanup
  ret i8 0
}

;--- helper-only.ll
target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"

declare ptr @malloc(i64)
declare void @sink(ptr)

; Both inputs have a static allocation size, so the call-escape check is
; elided.  The selected companion base is nevertheless real pass-created IR.
define void @helper_only_select(i1 %choose) {
; HELPER-LABEL: @helper_only_select(
; HELPER: %flexfat.base = select i1 %choose, ptr %a, ptr %b, !nosanitize
; HELPER-NOT: __flexfat_report_oob
; IDEMPOTENT-COUNT-1: %flexfat.base = select i1 %choose, ptr %a, ptr %b, !nosanitize
; INVALIDATE: Running analysis: DominatorTreeAnalysis on helper_only_select
; INVALIDATE: Running pass: FlexFatSanitizerPass on [module]
; INVALIDATE: Invalidating analysis: InnerAnalysisManagerProxy<AnalysisManager<Function>, Module> on [module]
  %a = call ptr @malloc(i64 16)
  %b = call ptr @malloc(i64 16)
  %selected = select i1 %choose, ptr %a, ptr %b
  call void @sink(ptr %selected)
  ret void
}
