; REQUIRES: !flexfat-custom-config
; RUN: opt -passes='flexfat<tbi;tbi-storage=shadow>,verify' -S %s | FileCheck %s --check-prefix=RAW --implicit-check-not='load atomic' --implicit-check-not='load volatile i8, ptr %flexfat.metadata'
; RUN: opt -passes='flexfat<tbi;tbi-storage=shadow>,default<O2>,verify' -S %s | FileCheck %s --check-prefix=OPT --implicit-check-not='load atomic'
; RUN: opt -passes='flexfat<tbi;tbi-storage=shadow>,function(early-cse<memssa>,instcombine,simplifycfg),verify' -S %s | FileCheck %s --check-prefix=OPT --implicit-check-not='load atomic'

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

; A metadata write may invalidate the first observation, even when the
; instrumented data reads themselves are volatile.
define i8 @metadata_write(ptr %p, ptr %metadata) {
; RAW-LABEL: define i8 @metadata_write(
; RAW: load i8, ptr %flexfat.metadata
; RAW: store i8 42, ptr %metadata
; RAW: load i8, ptr %flexfat.metadata
; OPT-LABEL: define i8 @metadata_write(
; OPT: load i8, ptr
; OPT: store i8 42, ptr %metadata
; OPT: load i8, ptr
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  store i8 42, ptr %metadata, !nosanitize !0
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %sum = add i8 %a, %b
  ret i8 %sum
}

define i8 @synchronization(ptr %p) {
; RAW-LABEL: define i8 @synchronization(
; RAW: load i8, ptr %flexfat.metadata
; RAW: fence acquire
; RAW: load i8, ptr %flexfat.metadata
; OPT-LABEL: define i8 @synchronization(
; OPT: load i8, ptr
; OPT: fence acquire
; OPT: load i8, ptr
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  fence acquire
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %sum = add i8 %a, %b
  ret i8 %sum
}
