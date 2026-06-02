; FlexFat Unit 7: a single load through a (fat) pointer argument gets an inlined
; bounds check. The base is computed by the inlined non-POW2 lowfat_base
; (reciprocal multiply); the check is the inlined lowfat_oob_check.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @load(ptr %p) {
entry:
  %v = load i32, ptr %p, align 4
  ret i32 %v
}

; CHECK-LABEL: define i32 @load(ptr %p)
;
; Inlined lowfat_base(%p): region index = p>>35, magic table load @ 0x300000,
; 128-bit reciprocal multiply, size table load @ 0x200000, base = objidx*size.
; CHECK:      ptrtoint ptr %p to i64
; CHECK:      lshr i64 %{{[^,]*}}, 35
; CHECK:      getelementptr i64, ptr inttoptr (i64 3145728 to ptr), i64
; CHECK:      load i64
; CHECK:      zext i64 %{{.*}} to i128
; CHECK:      mul i128
; CHECK:      lshr i128 %{{[^,]*}}, 64
; CHECK:      trunc i128 %{{.*}} to i64
; CHECK:      getelementptr i64, ptr inttoptr (i64 2097152 to ptr), i64
; CHECK:      load i64
; CHECK:      mul i64
; CHECK:      inttoptr i64 %{{.*}} to ptr
;
; Inlined lowfat_oob_check: idx = base>>35, size table reload, diff = ptr-base,
; unsigned (diff >=u size) compare, weighted branch to the cold error block.
; CHECK:      sub i64
; CHECK:      icmp uge i64
; CHECK:      br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[W:[0-9]+]]
; CHECK:      call void @lowfat_oob_error(i32 0, ptr %p, ptr %{{.*}})
; CHECK-NEXT: unreachable
; CHECK:      load i32, ptr %p
;
; CHECK:      ![[W]] = !{!"branch_weights", i32 1, i32 2000000000}
