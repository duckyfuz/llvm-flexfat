; RUN: not --crash opt -passes=flexfat -disable-output %s 2>&1 | FileCheck %s
; CHECK: FlexFat cannot recover scalable pointer-PHI escape provenance

target triple = "aarch64-unknown-linux-gnu"
target datalayout = "e-p:64:64-i64:64-i128:128-n32:64-S128"
define <vscale x 2 x i64> @unsupported(<vscale x 2 x ptr> %p) {
entry:
  br label %join
join:
  %joined = phi <vscale x 2 x ptr> [ %p, %entry ]
  %bits = ptrtoint <vscale x 2 x ptr> %joined to <vscale x 2 x i64>
  ret <vscale x 2 x i64> %bits
}
