; FlexFat Unit 10: -flexfat-check-whole-access. By default access_size is 0 (the
; check validates just the byte at ptr): `diff >=u size`. With the flag,
; access_size = sizeof(*ptr)-1, so the whole [ptr, ptr+sizeof) span is validated:
; `diff >=u size - (sizeof-1)`. For an i32 access that is `sub i64 %size, 3`.
;
; RUN: opt < %s -passes=flexfat -S | FileCheck %s --check-prefix=BYTE
; RUN: opt < %s -passes=flexfat -flexfat-check-whole-access -S | FileCheck %s --check-prefix=WHOLE

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

define i32 @load4(ptr %p, i64 %i) {
  %q = getelementptr i8, ptr %p, i64 %i
  %v = load i32, ptr %q
  ret i32 %v
}

; Default: no size adjustment -- compare diff directly against the table size.
; BYTE:     icmp uge i64
; BYTE-NOT: sub i64 %{{.*}}, 3

; Whole-access: the table size is reduced by sizeof(i32)-1 = 3 before the compare.
; WHOLE: sub i64 %{{.*}}, 3
; WHOLE: icmp uge i64
