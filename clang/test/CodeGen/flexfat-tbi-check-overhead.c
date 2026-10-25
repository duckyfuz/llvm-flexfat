// REQUIRES: aarch64-registered-target
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -O0 -emit-llvm -o - %s | FileCheck %s --check-prefixes=POINT,LAST
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-tbi=prior-byte -O0 -emit-llvm -o - %s | FileCheck %s --check-prefixes=POINT,PRIOR
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-check-whole-access -O0 -emit-llvm -o - %s | FileCheck %s --check-prefix=WHOLE
// RUN: %clang_cc1 -triple aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -O2 -emit-llvm -o - %s | FileCheck %s --check-prefix=LOOP

// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fsanitize-flexfat-check-whole-access -fno-sanitize-flexfat-check-whole-access -O0 -S -emit-llvm -o - %s | FileCheck %s --check-prefixes=POINT,LAST
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-check-whole-access -fsanitize-flexfat-check-whole-access -O0 -S -emit-llvm -o - %s | FileCheck %s --check-prefix=WHOLE

extern void *malloc(unsigned long);

int wide(volatile int *p) {
// POINT-LABEL: define{{.*}} @wide(
// LAST: %flexfat.metadata.address = add i64 %flexfat.base.int{{[0-9]*}}, {{.*}}
// PRIOR: %flexfat.metadata.address = sub i64 %flexfat.base.int{{[0-9]*}}, 1
// POINT: sub i64 {{.*}}, 1
// POINT: icmp uge i64
// POINT: call void @__flexfat_report_oob
// POINT-NEXT: unreachable
// POINT: load atomic i8, ptr %flexfat.metadata {{acquire|monotonic}}
// POINT: call void @__flexfat_report_temporal_v3
// POINT: load volatile i32
// WHOLE-LABEL: define{{.*}} @wide(
// WHOLE: icmp ugt i64 4,
  return *p;
}

unsigned char indexed(unsigned long n) {
// LOOP-LABEL: define{{.*}} @indexed(
// LOOP: call{{.*}} @malloc(i64 noundef 64)
// LOOP: %flexfat.metadata.address = add i64
// LOOP: %flexfat.metadata =
// LOOP: for.body:
// LOOP-NOT: flexfat.metadata.address
// LOOP: call void @__flexfat_report_oob
// LOOP: load atomic i8, ptr %flexfat.metadata {{acquire|monotonic}}
// LOOP: load volatile i8
  unsigned char *p = malloc(64);
  unsigned char value = 0;
  for (unsigned long i = 0; i < n; ++i)
    value = *(volatile unsigned char *)(p + (i & 15));
  return value;
}
