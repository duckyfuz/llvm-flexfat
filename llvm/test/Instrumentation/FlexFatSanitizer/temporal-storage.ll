; RUN: opt -passes='flexfat<tbi;tbi-storage=last-byte>,verify' -S %s | FileCheck %s --check-prefix=COMMON
; RUN: opt -passes='flexfat<tbi;tbi-storage=prior-byte>,verify' -S %s | FileCheck %s --check-prefix=COMMON
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi;tbi-storage=shadow>,verify' -S %s | FileCheck %s --check-prefixes=COMMON,SHADOW --implicit-check-not='load atomic' %}
; RUN: %if flexfat-custom-config %{ not opt -passes='flexfat<tbi;tbi-storage=shadow>' -disable-output %s 2>&1 | FileCheck %s --check-prefix=REJECT %}
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi;tbi-storage=last-byte>' -S %s | FileCheck %s --check-prefix=POW2 --implicit-check-not='icmp ne i8' --implicit-check-not='= or i1' %}
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi;tbi-storage=prior-byte>' -S %s | FileCheck %s --check-prefix=POW2 --implicit-check-not='icmp ne i8' --implicit-check-not='= or i1' %}
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi;tbi-storage=shadow>' -S %s | FileCheck %s --check-prefix=POW2 --implicit-check-not='icmp ne i8' --implicit-check-not='= or i1' %}
; REJECT: FlexFat TBI shadow storage requires a POW2 build

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

define i8 @twice(ptr %p) {
; POW2-LABEL: define i8 @twice(
; POW2: [[GEN:%.*]] = load{{( atomic)?}} i8, ptr {{.*}}{{( monotonic)?}}, align 1
; POW2-NEXT: [[SHIFT:%.*]] = lshr i64 {{.*}}, 56
; POW2-NEXT: [[TAG:%.*]] = trunc i64 [[SHIFT]] to i8
; POW2-NEXT: [[MATCH:%.*]] = icmp eq i8 [[TAG]], [[GEN]]
; POW2-NEXT: [[FAIL:%.*]] = xor i1 [[MATCH]], true
; POW2-NEXT: br i1 [[FAIL]]
; POW2: %a = load volatile i8, ptr %p
; POW2: load{{( atomic)?}} i8, ptr {{.*}}{{( monotonic)?}}, align 1
; POW2: %b = load volatile i8, ptr %p
; COMMON-LABEL: define i8 @twice(
; COMMON: [[ENTRY:%flexfat.metadata]] = inttoptr
; COMMON: [[FIRST:%flexfat.generation]] = load{{( atomic)?}} i8, ptr [[ENTRY]]{{( monotonic| acquire)?}}, align 1
; COMMON: icmp eq i8 {{.*}}, [[FIRST]]
; COMMON: call void @__flexfat_report_temporal_v3
; COMMON: %a = load volatile i8, ptr %p
; COMMON: [[SECOND:%flexfat.generation[0-9]*]] = load{{( atomic)?}} i8, ptr [[ENTRY]]{{( monotonic| acquire)?}}, align 1
; COMMON: icmp eq i8 {{.*}}, [[SECOND]]
; COMMON: call void @__flexfat_report_temporal_v3
; COMMON: %b = load volatile i8, ptr %p
; SHADOW: call void @__flexfat_tbi_abi_v7()
  %a = load volatile i8, ptr %p, !flexfat.instrumented !0
  %b = load volatile i8, ptr %p, !flexfat.instrumented !0
  %sum = add i8 %a, %b
  ret i8 %sum
}
!0 = !{}
