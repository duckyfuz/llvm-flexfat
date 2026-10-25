// REQUIRES: aarch64-registered-target
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -O2 -x c-header %s -o %t.pch
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -O2 -include-pch %t.pch -S -emit-llvm %s -o - | FileCheck %s
// CHECK-LABEL: define {{.*}} @read_value(
// CHECK: load i8, ptr
// CHECK: call void @__flexfat_report_temporal_v3

#ifndef FLEXFAT_PCH_TEST
#define FLEXFAT_PCH_TEST
int read_value(volatile int *p) { return *p; }
#endif
