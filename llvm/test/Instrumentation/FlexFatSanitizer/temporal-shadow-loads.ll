; REQUIRES: !flexfat-custom-config
; RUN: opt -passes='flexfat<tbi;tbi-storage=shadow>,verify' -S %s | FileCheck %s --check-prefix=RAW --implicit-check-not='load atomic' --implicit-check-not='load volatile i8, ptr %flexfat.metadata'
; RUN: opt -passes='flexfat<tbi;tbi-storage=shadow>,default<O2>,verify' -S %s | FileCheck %s --check-prefix=OPT --implicit-check-not='load atomic'

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

declare void @may_free(ptr)

; Reuse the metadata address, but emit ordinary observations on either side
; of a call that can invalidate the allocation. Optimization must retain both.
define i8 @separated(ptr %p) {
; RAW-LABEL: define i8 @separated(
; RAW: [[ENTRY:%flexfat.metadata]] = inttoptr
; RAW: load i8, ptr [[ENTRY]], align 1
; RAW: %a = load volatile i8, ptr %p
; RAW: call void @may_free(ptr %p)
; RAW: load i8, ptr [[ENTRY]], align 1
; RAW: %b = load volatile i8, ptr %p
; OPT-LABEL: define i8 @separated(
; OPT: load i8, ptr {{.*}}, align 1
; OPT: load volatile i8, ptr %p
; OPT: call void @may_free(ptr {{.*}}%p)
; OPT: load i8, ptr {{.*}}, align 1
; OPT: load volatile i8, ptr %p
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  call void @may_free(ptr %p)
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %sum = add i8 %a, %b
  ret i8 %sum
}

; With no intervening writes or calls, ordinary shadow loads can be reused.
define i8 @adjacent(ptr %p) {
; RAW-LABEL: define i8 @adjacent(
; RAW: load i8, ptr %flexfat.metadata, align 1
; RAW: %a = load volatile i8, ptr %p
; RAW: load i8, ptr %flexfat.metadata, align 1
; RAW: %b = load volatile i8, ptr %p
; OPT-LABEL: define i8 @adjacent(
; OPT: load i8, ptr {{.*}}, align 1
; OPT: load volatile i8, ptr %p
; OPT-NOT: load i8, ptr
; OPT: load volatile i8, ptr %p
; OPT-NOT: load i8, ptr
; OPT: ret i8
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %sum = add i8 %a, %b
  ret i8 %sum
}

!0 = !{}
