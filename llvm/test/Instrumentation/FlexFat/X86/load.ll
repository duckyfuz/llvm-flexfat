; FlexFat Unit 7/8: a load through a fat pointer at a dynamic (unprovable) offset
; gets an inlined bounds check. The base is computed by the inlined non-POW2
; lowfat_base (reciprocal multiply); the check is the inlined lowfat_oob_check.
; (A *direct* deref of an input pointer is now elided by Unit 8's bounds
; analysis, so this uses a dynamic GEP, which stays checked.)
;
; Unit 17: CHECK lines verify the non-POW2 reciprocal-multiply lowering
; (`mul i128`, `_LOWFAT_MAGICS` load at 0x300000). The POW2 variant emits
; `and i64 %iptr, %magic` instead and has no SIZES-table multiply.
; REQUIRES: flexfat-nonpow2
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @load(ptr %p, i64 %i) {
entry:
  %q = getelementptr i8, ptr %p, i64 %i
  %v = load i32, ptr %q, align 4
  ret i32 %v
}

; CHECK-LABEL: define i32 @load(ptr %p, i64 %i)
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
; The error block (the TRUE successor, containing lowfat_oob_error) must carry
; the COLD weight: !prof = {1, 2000000000}, i.e. error-edge=1, fast-edge=2e9.
; This direction is intentionally inverted from the reference (see STATUS.md).
; CHECK:      sub i64
; CHECK:      icmp uge i64
; CHECK:      br i1 %{{.*}}, label %[[ERR:[0-9]+]], label %[[CONT:[0-9]+]], !prof ![[W:[0-9]+]]
; CHECK:    [[ERR]]:
; CHECK:      call void @lowfat_oob_error(i32 0, ptr %q, ptr %{{.*}})
; CHECK-NEXT: unreachable
; CHECK:    [[CONT]]:
; CHECK:      load i32, ptr %q
;
; CHECK:      ![[W]] = !{!"branch_weights", i32 1, i32 2000000000}
