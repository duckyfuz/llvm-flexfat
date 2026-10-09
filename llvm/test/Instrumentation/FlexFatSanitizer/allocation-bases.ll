; RUN: opt < %s -passes='flexfat,verify' -S | FileCheck %s --check-prefix=CHECK
; RUN: clang -x ir -O0 -fsanitize=flexfat -S -emit-llvm %s -o - | FileCheck %s --check-prefix=CLANG
; RUN: clang -x ir -O0 -fsanitize=flexfat -mllvm -flexfat-alignment=right -S -emit-llvm %s -o - | FileCheck %s --check-prefix=RIGHT

target datalayout = "e-m:e-i64:64-i128:128-n32:64-S128"

declare ptr @malloc(i64)
declare ptr @calloc(i64, i64)
declare ptr @realloc(ptr, i64)
declare ptr @strdup(ptr)
declare ptr @aligned_alloc(i64, i64)
declare ptr @_Znwm(i64)
declare ptr @_ZnwmSt11align_val_t(i64, i64)
declare ptr @unknown_pointer()
declare i32 @__gxx_personality_v0(...)

; Ordinary allocator returns are reused directly for left alignment. The size
; lookup still clamps the direct base, keeping system-allocation fallback on
; index-zero sentinel metadata.
define i8 @ordinary_malloc(i64 %offset) {
; CHECK-LABEL: @ordinary_malloc(
; CHECK: %p = call ptr @malloc(i64 64)
; CHECK-NEXT: %q = getelementptr i8, ptr %p, i64 %offset
; CHECK-NOT: %flexfat.root.int = ptrtoint ptr %p
; CHECK: ptrtoint ptr %p to i64
; CHECK: icmp ult i64 {{.*}}, 281474976710656
; CLANG-LABEL: @ordinary_malloc(
; CLANG: %p = call ptr @malloc(i64 64)
; CLANG-NEXT: %q = getelementptr i8, ptr %p, i64 %offset
; CLANG-NOT: %flexfat.root.int = ptrtoint ptr %p
; CLANG: ptrtoint ptr %p to i64
; RIGHT-LABEL: @ordinary_malloc(
; RIGHT: %p = call ptr @malloc(i64 64)
; RIGHT-NEXT: %flexfat.root.int = ptrtoint ptr %p to i64
  %p = call ptr @malloc(i64 64)
  %q = getelementptr i8, ptr %p, i64 %offset
  %value = load i8, ptr %q
  ret i8 %value
}

define i8 @ordinary_families(i64 %offset, ptr %old, ptr %source) {
; CHECK-LABEL: @ordinary_families(
; CHECK: %calloc = call ptr @calloc(i64 4, i64 16)
; CHECK: %realloc = call ptr @realloc(ptr %old, i64 64)
; CHECK: %duplicate = call ptr @strdup(ptr %source)
; CHECK: %new = call ptr @_Znwm(i64 64)
; CHECK: %selected = select i1
; CHECK-NOT: %flexfat.root.int = ptrtoint ptr %calloc
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %realloc
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %duplicate
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %new
  %calloc = call ptr @calloc(i64 4, i64 16)
  %realloc = call ptr @realloc(ptr %old, i64 64)
  %duplicate = call ptr @strdup(ptr %source)
  %new = call ptr @_Znwm(i64 64)
  %pick0 = icmp eq ptr %old, null
  %pick1 = icmp eq ptr %source, null
  %left = select i1 %pick0, ptr %calloc, ptr %realloc
  %right = select i1 %pick1, ptr %duplicate, ptr %new
  %selected = select i1 %pick0, ptr %left, ptr %right
  %q = getelementptr i8, ptr %selected, i64 %offset
  %value = load i8, ptr %q
  ret i8 %value
}

; Aligned allocation families may return an interior address and retain full
; recovery even outside right-align mode.
define i8 @aligned_families(i64 %offset) {
; CHECK-LABEL: @aligned_families(
; CHECK: %c = call ptr @aligned_alloc(i64 64, i64 128)
; CHECK-NEXT: %flexfat.root.int = ptrtoint ptr %c to i64
; CHECK: %cpp = call ptr @_ZnwmSt11align_val_t(i64 128, i64 64)
; CHECK-NEXT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %cpp to i64
  %c = call ptr @aligned_alloc(i64 64, i64 128)
  %cq = getelementptr i8, ptr %c, i64 %offset
  %cv = load i8, ptr %cq
  %cpp = call ptr @_ZnwmSt11align_val_t(i64 128, i64 64)
  %cppq = getelementptr i8, ptr %cpp, i64 %offset
  %cppv = load i8, ptr %cppq
  %sum = add i8 %cv, %cppv
  ret i8 %sum
}

; A musttail result must remain immediately adjacent to its return.  Its
; zero-width return escape is statically valid, so no base recovery is needed.
define ptr @musttail_aligned_alloc(i64 %alignment, i64 %size) {
; CHECK-LABEL: @musttail_aligned_alloc(
; CHECK: %p = musttail call ptr @aligned_alloc(i64 %alignment, i64 %size)
; CHECK-NEXT: ret ptr %p
  %p = musttail call ptr @aligned_alloc(i64 %alignment, i64 %size)
  ret ptr %p
}

; Unknown call results also retain recovery.
define i8 @unknown_result(i64 %offset) {
; CHECK-LABEL: @unknown_result(
; CHECK: %p = call ptr @unknown_pointer()
; CHECK-NEXT: %flexfat.root.int = ptrtoint ptr %p to i64
  %p = call ptr @unknown_pointer()
  %q = getelementptr i8, ptr %p, i64 %offset
  %value = load i8, ptr %q
  ret i8 %value
}

; Direct allocation bases flow through companion PHIs without recovery.
define i8 @allocation_phi(i1 %choose, i64 %offset) {
; CHECK-LABEL: @allocation_phi(
; CHECK: %a = call ptr @malloc(i64 64)
; CHECK-NEXT: br label %merge
; CHECK: %b = call ptr @calloc(i64 1, i64 64)
; CHECK-NEXT: br label %merge
; CHECK: %root = phi ptr
; CHECK-NEXT: %flexfat.base = phi ptr [ %a, %left ], [ %b, %right ]
; CHECK-NOT: %flexfat.root.int = ptrtoint ptr %a
; CHECK-NOT: %flexfat.root.int{{[0-9]*}} = ptrtoint ptr %b
entry:
  br i1 %choose, label %left, label %right
left:
  %a = call ptr @malloc(i64 64)
  br label %merge
right:
  %b = call ptr @calloc(i64 1, i64 64)
  br label %merge
merge:
  %root = phi ptr [ %a, %left ], [ %b, %right ]
  %q = getelementptr i8, ptr %root, i64 %offset
  %value = load i8, ptr %q
  ret i8 %value
}

; Ordinary allocation invokes keep their normal edge intact, while an aligned
; invoke still splits its normal edge to hold recovery.
define i8 @ordinary_invoke(i64 %offset) personality ptr @__gxx_personality_v0 {
; CHECK-LABEL: @ordinary_invoke(
; CHECK: %p = invoke ptr @malloc(i64 64)
; CHECK-NEXT: to label %normal unwind label %exception
; CHECK: normal:
; CHECK-NEXT: %q = getelementptr i8, ptr %p, i64 %offset
entry:
  %p = invoke ptr @malloc(i64 64) to label %normal unwind label %exception
normal:
  %q = getelementptr i8, ptr %p, i64 %offset
  %value = load i8, ptr %q
  ret i8 %value
exception:
  %landing = landingpad { ptr, i32 } cleanup
  ret i8 0
}

define i8 @aligned_invoke(i64 %offset) personality ptr @__gxx_personality_v0 {
; CHECK-LABEL: @aligned_invoke(
; CHECK: %p = invoke ptr @aligned_alloc(i64 64, i64 128)
; CHECK: to label %normal.split unwind label %exception
; CHECK: normal.split:
; CHECK-NEXT: %flexfat.root.int = ptrtoint ptr %p to i64
entry:
  %p = invoke ptr @aligned_alloc(i64 64, i64 128)
      to label %normal unwind label %exception
normal:
  %q = getelementptr i8, ptr %p, i64 %offset
  %value = load i8, ptr %q
  ret i8 %value
exception:
  %landing = landingpad { ptr, i32 } cleanup
  ret i8 0
}
