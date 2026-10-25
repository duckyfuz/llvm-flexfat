// RUN: %clang -target aarch64-linux-gnu -O1 -fno-vectorize -fno-unroll-loops -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte %s -o - | FileCheck %s

long conditional(const volatile long *p, const volatile long *q,
                 long n, int enabled) {
  long sum = 0;
  for (long i = 0; i < n; ++i)
    if (enabled)
      sum += p[i] + q[i];
  return sum;
}

// CHECK-LABEL: define{{.*}} @conditional(
// CHECK: br i1 {{.*}}, label %{{.*}}, label %[[ACCESS:[a-zA-Z0-9._]+]]
// CHECK: [[ACCESS]]:
// CHECK: call void @__flexfat_report_oob
// CHECK: load i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK: load volatile i64
// CHECK: call void @__flexfat_report_oob
// CHECK: load i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK: load volatile i64
// CHECK: br i1 {{.*}}, label %{{.*}}, label %{{.*}}

long rows(const volatile long *const *grid, long height, long width) {
  long sum = 0;
  for (long r = 0; r < height; ++r) {
    const volatile long *row = grid[r];
    for (long i = 0; i < width; ++i)
      sum += row[i];
  }
  return sum;
}

// CHECK-LABEL: define{{.*}} @rows(
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK: %[[ROW:[a-zA-Z0-9._]+]] = load ptr
// CHECK: getelementptr inbounds{{( nuw)?}} i64, ptr %[[ROW]]
// CHECK: call void @__flexfat_report_oob
// CHECK: load i8
// CHECK: call void @__flexfat_report_temporal_v3
// CHECK: load volatile i64
