// REQUIRES: aarch64-registered-target
// UNSUPPORTED: flexfat-custom-config
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=packed-shadow -O2 -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=packed-shadow -O2 -S -emit-llvm -o - %s | FileCheck %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=packed-shadow -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=RAW
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=packed-shadow -O2 -S -o - %s | FileCheck %s --check-prefix=ASM
// RAW: and i64 {{.*}}, 4294967295
// RAW: sub i64 {{.*}}, 8589934592
// RAW: ashr i64
// RAW: select i1 {{.*}}, i64 {{.*}}, i64 0
// RAW: getelementptr i8, ptr inttoptr (i64 35184372088832 to ptr), i64
// CHECK-LABEL: define {{.*}} @read_byte(
// CHECK: and i64 {{.*}}, 4294967295
// CHECK: {{(add|or disjoint)}} {{.*}}i64 {{.*}}, -8589934592
// CHECK: ashr i64
// CHECK: select i1 {{.*}}, i64 {{.*}}, i64 0
// CHECK: %flexfat.metadata = getelementptr i8, ptr inttoptr (i64 35184372088832 to ptr), i64
// CHECK: load i8, ptr {{.*}}!nosanitize
// CHECK: icmp eq i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK: call void @__flexfat_tbi_abi_packed_shadow_pow2_v1()
// ASM-LABEL: read_byte:
// ASM: csel x[[OFFSET:[0-9]+]], x{{[0-9]+}}, xzr, lo
// ASM-NOT: add
// ASM: b.hs
// ASM-NOT: add
// ASM: ldrb w[[GEN:[0-9]+]], [x[[OFFSET]], x{{[0-9]+}}]
// ASM-NEXT: cmp x[[GEN]], x0, lsr #56
// ASM-NEXT: b.ne
unsigned read_byte(volatile unsigned char *p) { return *p; }
