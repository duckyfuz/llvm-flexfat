; REQUIRES: aarch64-registered-target
; RUN: opt -passes='flexfat<tbi>,default<O2>,verify' -S %s | llc -mtriple=aarch64-linux-gnu -o - | FileCheck %s --implicit-check-not='udiv' --implicit-check-not='sdiv' --implicit-check-not='__flexfat_check_temporal'
; RUN: %if flexfat-custom-config %{ opt -passes='flexfat<tbi>,default<O2>' -S %s | llc -mtriple=aarch64-linux-gnu -o - | FileCheck %s --check-prefix=CUSTOM %}
; RUN: %if !flexfat-custom-config %{ opt -passes='flexfat<tbi>,default<O2>' -S %s | llc -mtriple=aarch64-linux-gnu -o - | FileCheck %s --check-prefix=POW2 %}

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"

; Isolate temporal code generation from spatial checks. The successful path
; retains the tagged address in x0 and has neither a call nor diagnostic spills.
define i8 @read_byte(ptr %p) {
; CHECK-LABEL: read_byte:
; CHECK-NOT: bl{{[ \t]}}
; CHECK-NOT: stp
; CHECK-NOT: str
; CHECK-NOT: {{^[ \t]*(b\.[a-z]+|cbn?z|tbn?z)}}
; CHECK: ldarb
; CHECK-NOT: bl{{[ \t]}}
; CHECK-NOT: stp
; CHECK-NOT: str
; CHECK: b.ne
; CHECK-NOT: {{^[ \t]*(b\.[a-z]+|cbn?z|tbn?z)}}
; CHECK: ldrb w0, [x0]
; CHECK-NEXT: ret
; CHECK: bl __flexfat_report_temporal_v3
; CUSTOM-LABEL: read_byte:
; CUSTOM: umulh
; CUSTOM: mul
; CUSTOM: ldarb
; POW2-LABEL: read_byte:
; POW2: lsr x{{[0-9]+}}, x{{[0-9]+}}, x{{[0-9]+}}
; POW2: ldarb
; POW2: b.ne
; POW2-NOT: {{^[ \t]*(b\.[a-z]+|cbn?z|tbn?z)}}
; POW2: ldrb w0, [x0]
  %v = load volatile i8, ptr %p, !flexfat.instrumented !0
  ret i8 %v
}
!0 = !{}
