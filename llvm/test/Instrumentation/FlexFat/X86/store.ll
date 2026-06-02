; FlexFat Unit 7: a single store through a GEP off a (fat) pointer argument gets
; an inlined bounds check. calcBasePtr recurses GEP -> argument; the access
; pointer is the GEP; info = WRITE (1).
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define void @store(ptr %p, i64 %i) {
entry:
  %q = getelementptr inbounds i8, ptr %p, i64 %i
  store i8 42, ptr %q, align 1
  ret void
}

; CHECK-LABEL: define void @store(ptr %p, i64 %i)
;
; Base is computed from %p (the GEP's pointer operand), not %q.
; CHECK:      ptrtoint ptr %p to i64
; CHECK:      getelementptr i64, ptr inttoptr (i64 3145728 to ptr), i64
; CHECK:      mul i128
; CHECK:      getelementptr i64, ptr inttoptr (i64 2097152 to ptr), i64
; CHECK:      inttoptr i64 %{{.*}} to ptr
;
; The check validates the access pointer %q; info = WRITE = 1.
; CHECK:      icmp uge i64
; CHECK:      br i1 %{{.*}}, label %{{.*}}, label %{{.*}}, !prof ![[W:[0-9]+]]
; CHECK:      call void @lowfat_oob_error(i32 1, ptr %q, ptr %{{.*}})
; CHECK-NEXT: unreachable
; CHECK:      store i8 42, ptr %q
;
; CHECK:      ![[W]] = !{!"branch_weights", i32 1, i32 2000000000}
