// REQUIRES: !flexfat-custom-config
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=last-byte -mllvm -flexfat-tbi-ordinary-loads=true -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not='load atomic'
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=prior-byte -mllvm -flexfat-tbi-ordinary-loads=true -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not='load atomic'
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=shadow -mllvm -flexfat-tbi-ordinary-loads=true -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not='load atomic'
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi -mllvm -flexfat-tbi-storage=packed-shadow -mllvm -flexfat-tbi-ordinary-loads=true -O2 -emit-llvm -o - %s | FileCheck %s --implicit-check-not='load atomic'

// CHECK-LABEL: define{{.*}} @read_value(
// CHECK: load i8, ptr
// CHECK: icmp eq i8
// CHECK: load i32, ptr
int read_value(int *p) { return *p; }
