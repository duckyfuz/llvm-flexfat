declare ptr @malloc(i64)

define void @loop_spill(ptr %slot, i64 %offset, i1 %update) {
entry:
  %input = call ptr @malloc(i64 16)
  %defined = getelementptr i8, ptr %input, i64 %offset
  br label %loop
loop:
  %p = phi ptr [ undef, %entry ], [ %defined, %updated ], [ %p, %carry ]
  %n = phi i32 [ 0, %entry ], [ %inc, %updated ], [ %inc, %carry ]
  store ptr %p, ptr %slot
  %inc = add i32 %n, 1
  %again = icmp ult i32 %inc, 2
  br i1 %again, label %choice, label %exit
choice:
  br i1 %update, label %updated, label %carry
updated:
  br label %loop
carry:
  br label %loop
exit:
  ret void
}

define void @bad_destination() {
  %input = call ptr @malloc(i64 16)
  %dest = getelementptr i8, ptr %input, i64 256
  store ptr undef, ptr %dest
  ret void
}

; A non-inbounds GEP can legally cross a slot boundary before the escape
; check. The check must retain the allocation base from the source PHI.
define void @derived_spill(ptr %slot, i64 %offset, i1 %take) {
entry:
  %input = call ptr @malloc(i64 16)
  br i1 %take, label %defined, label %undefined
defined:
  br label %join
undefined:
  br label %join
join:
  %p = phi ptr [ %input, %defined ], [ undef, %undefined ]
  %q = getelementptr i8, ptr %p, i64 %offset
  store ptr %q, ptr %slot
  ret void
}
