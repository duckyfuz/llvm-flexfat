// RUN: %clang -target aarch64-linux-gnu -O1 -fno-vectorize -fno-unroll-loops -S -emit-llvm -fsanitize=flexfat -mllvm -flexfat-tbi=true %s -o - | FileCheck %s --implicit-check-not=flexfat.fast --implicit-check-not=flexfat.trip

extern void tick(long);

__attribute__((noinline)) long grouped(const volatile long *p,
                                       const volatile long *q, long n) {
  long sum = 0;
  for (long i = 0; i < n; ++i) {
    if (i & 1)
      sum += p[i];
    sum += p[i + 1] + p[i + 2] + p[i + 3] + p[i + 4];
    sum += q[i * i];
    tick(i);
  }
  return sum;
}

__attribute__((noinline)) long many(const volatile long *p, long n) {
  long sum = 0;
  for (long i = 0; i < n; ++i)
    sum += p[i] + p[i+1] + p[i+2] + p[i+3] + p[i+4] +
           p[i+5] + p[i+6] + p[i+7] + p[i+8] + p[i+9] +
           p[i+10] + p[i+11] + p[i+12] + p[i+13] + p[i+14] +
           p[i+15] + p[i+16] + p[i+17] + p[i+18] + p[i+19];
  return sum;
}

__attribute__((noinline)) long two_roots(const volatile long *a,
                                         const volatile long *b, long n) {
  long sum = 0;
  for (long i = 0; i < n; ++i)
    sum += a[i] + a[i+1] + b[i] + b[i+1];
  return sum;
}

__attribute__((noinline)) long rows(const volatile long *const *grid,
                                    long height, long width) {
  long sum = 0;
  for (long r = 0; r < height; ++r) {
    const volatile long *row = grid[r];
    for (long i = 0; i < width; ++i)
      sum += row[i] + row[i+1] + row[i+2] + row[i+3];
  }
  return sum;
}

// Small loops retain ordinary spatial and temporal checks too.
__attribute__((noinline)) long small(const volatile long *p, long n) {
  long sum = 0;
  for (long i = 0; i < n; ++i)
    sum += p[i] + p[i + 1];
  return sum;
}

// Loop accesses retain spatial checks and per-access generation checks.
// CHECK-LABEL: define{{.*}} @grouped(
// CHECK: call void @__flexfat_report_oob
// CHECK: load atomic i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK-LABEL: define{{.*}} @many(
// CHECK: call void @__flexfat_report_oob
// CHECK: load atomic i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK-LABEL: define{{.*}} @two_roots(
// CHECK: call void @__flexfat_report_oob
// CHECK: load atomic i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK-LABEL: define{{.*}} @rows(
// CHECK: call void @__flexfat_report_oob
// CHECK: load atomic i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK-LABEL: define{{.*}} @small(
// CHECK: call void @__flexfat_report_oob
// CHECK: load atomic i8
// CHECK: call void @__flexfat_report_temporal_v3
