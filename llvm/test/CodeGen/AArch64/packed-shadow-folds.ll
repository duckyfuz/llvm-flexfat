; RUN: llc -mtriple=aarch64-linux-gnu -verify-machineinstrs < %s | FileCheck %s

; Truncating a high byte should not prevent a 64-bit shifted CMP. The
; generation is also used by the caller's failure report.
define i1 @tag_equal(i64 %tagged, ptr %metadata, ptr %observed) {
; CHECK-LABEL: tag_equal:
; CHECK: ldrb w[[GEN:[0-9]+]], [x1]
; CHECK-NOT: lsr
; CHECK: cmp x[[GEN]], x0, lsr #56
; CHECK: cset w0, eq
  %g = load i8, ptr %metadata
  store i8 %g, ptr %observed
  %s = lshr i64 %tagged, 56
  %t = trunc i64 %s to i8
  %eq = icmp eq i8 %g, %t
  ret i1 %eq
}

define i1 @tag_unequal(i64 %tagged, ptr %metadata) {
; CHECK-LABEL: tag_unequal:
; CHECK: ldrb w[[GEN:[0-9]+]], [x1]
; CHECK-NOT: lsr
; CHECK: cmp x[[GEN]], x0, lsr #56
; CHECK: cset w0, ne
  %g = load i8, ptr %metadata
  %s = lshr i64 %tagged, 56
  %t = trunc i64 %s to i8
  %eq = icmp ne i8 %t, %g
  ret i1 %eq
}

; Bits discarded by the truncation must not participate in the compare.
define i1 @middle_byte(i64 %tagged, ptr %metadata) {
; CHECK-LABEL: middle_byte:
; CHECK-NOT: cmp {{.*}}, x0, lsr #24
; CHECK: ret
  %g = load i8, ptr %metadata
  %s = lshr i64 %tagged, 24
  %t = trunc i64 %s to i8
  %eq = icmp eq i8 %g, %t
  ret i1 %eq
}

; Both select arms must be safe, including foreign pointers selecting zero
; offset at the anchor. Reuse the comparison flags in the select.
define i8 @packed_load(i64 %class, i64 %offset) {
; CHECK-LABEL: packed_load:
; CHECK: cmp x0, #27
; CHECK: csel x[[OFFSET:[0-9]+]], x1, xzr, lo
; CHECK: ldrb w0, [x[[OFFSET]], x{{[0-9]+}}]
; CHECK: ret
  %managed = icmp ult i64 %class, 27
  %selected = select i1 %managed, i64 %offset, i64 0
  %p = getelementptr i8, ptr inttoptr (i64 35184372088832 to ptr), i64 %selected
  %v = load i8, ptr %p
  ret i8 %v
}

define i8 @packed_load_inverse(i64 %class, i64 %offset) {
; CHECK-LABEL: packed_load_inverse:
; CHECK: cmp x0, #27
; CHECK: csel x[[OFFSET:[0-9]+]], xzr, x1, lo
; CHECK: ldrb w0, [x[[OFFSET]], x{{[0-9]+}}]
; CHECK: ret
  %foreign = icmp ult i64 %class, 27
  %selected = select i1 %foreign, i64 0, i64 %offset
  %p = getelementptr i8, ptr inttoptr (i64 35184372088832 to ptr), i64 %selected
  %v = load i8, ptr %p
  ret i8 %v
}

; Preserve volatile memory operands when changing the address.
define i8 @packed_volatile_load(i64 %class, i64 %offset) {
; CHECK-LABEL: packed_volatile_load:
; CHECK: csel x[[OFFSET:[0-9]+]], x1, xzr, lo
; CHECK: ldrb w0, [x[[OFFSET]], x{{[0-9]+}}]
; CHECK: ret
  %managed = icmp ult i64 %class, 27
  %selected = select i1 %managed, i64 %offset, i64 0
  %p = getelementptr i8, ptr inttoptr (i64 35184372088832 to ptr), i64 %selected
  %v = load volatile i8, ptr %p
  ret i8 %v
}

; Do not split an address shared with an escaping use.
define i8 @shared_address(i64 %class, i64 %offset, ptr %out) {
; CHECK-LABEL: shared_address:
; CHECK: add
; CHECK: csel
; CHECK: str
; CHECK: ldrb w0, [x{{[0-9]+}}]
; CHECK: ret
  %managed = icmp ult i64 %class, 27
  %sum = add i64 %offset, 35184372088832
  %address = select i1 %managed, i64 %sum, i64 35184372088832
  store i64 %address, ptr %out
  %p = inttoptr i64 %address to ptr
  %v = load i8, ptr %p
  ret i8 %v
}
