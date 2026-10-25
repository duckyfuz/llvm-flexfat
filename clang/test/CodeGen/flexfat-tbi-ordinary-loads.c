// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi-ordinary-loads -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not='load atomic'
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=prior-byte -fsanitize-flexfat-tbi-ordinary-loads -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not='load atomic'

// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi-ordinary-loads -fno-sanitize-flexfat-tbi-ordinary-loads -O2 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=ATOMIC
// ATOMIC: load atomic i8, ptr {{.*}} {{monotonic|acquire}}

// CHECK-LABEL: define{{.*}} @read_value(
// CHECK: load i8, ptr
// CHECK: icmp {{eq|ne}} i8
// CHECK: load i32, ptr
int read_value(int *p) { return *p; }
