// RUN: %clang -target aarch64-linux-gnu -O1 -fno-vectorize -fno-unroll-loops -S -emit-llvm -fsanitize=flexfat -mllvm -flexfat-tbi=true -mllvm -flexfat-version-tbi-loops=true %s -o - | FileCheck %s
// RUN: %clang -target aarch64-linux-gnu -O1 -fno-vectorize -fno-unroll-loops -S -fsanitize=flexfat -mllvm -flexfat-tbi=true -mllvm -flexfat-version-tbi-loops=true %s -o - | FileCheck %s --check-prefix=ASM
// RUN: %clang -target aarch64-linux-gnu -O1 -fno-vectorize -fno-unroll-loops -S -emit-llvm -fsanitize=flexfat -mllvm -flexfat-tbi=true -mllvm -flexfat-version-tbi-loops=false %s -o - | FileCheck %s --check-prefix=OFF

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

// The extended policy must retain the default versioned path for a small,
// otherwise eligible loop.
__attribute__((noinline)) long small(const volatile long *p, long n) {
  long sum = 0;
  for (long i = 0; i < n; ++i)
    sum += p[i] + p[i + 1];
  return sum;
}

// CHECK-LABEL: define{{.*}} @grouped(
// CHECK: flexfat.metadata
// CHECK: llvm.sadd.with.overflow.i64
// CHECK: br i1 {{.*}}, label %for.body.flexfat.fast, label %for.body
// CHECK: for.body.flexfat.fast:
// CHECK: load atomic i8, ptr %flexfat.metadata acquire
// CHECK: load atomic i8, ptr %flexfat.metadata acquire
// CHECK: load atomic i8, ptr %flexfat.metadata acquire
// CHECK: load atomic i8, ptr %flexfat.metadata acquire
// CHECK: load atomic i8, ptr %flexfat.metadata acquire
// CHECK: %flexfat.metadata{{[0-9]+}} = inttoptr
// CHECK: call void @__flexfat_report_oob
// CHECK: for.body:
// CHECK: flexfat.metadata{{[0-9]+}} = inttoptr
// CHECK: call void @__flexfat_report_oob
// CHECK-LABEL: define{{.*}} @many(
// CHECK: flexfat.metadata
// CHECK: for.body.flexfat.fast:
// CHECK-LABEL: define{{.*}} @two_roots(
// CHECK: flexfat.metadata
// CHECK: flexfat.metadata
// CHECK: for.body.flexfat.fast:
// CHECK-LABEL: define{{.*}} @rows(
// CHECK: flexfat.metadata
// CHECK: for.body{{.*}}flexfat.fast:
// CHECK-LABEL: define{{.*}} @small(
// CHECK: for.body.flexfat.fast:
// ASM-LABEL: grouped:
// ASM: // %for.body.flexfat.fast
// ASM: ldarb
// ASM: ldarb
// ASM: ldarb
// ASM: ldarb
// ASM: ldarb
// ASM: bl{{[[:space:]]+}}tick
// OFF-LABEL: define{{.*}} @many(
// OFF-NOT: flexfat.fast
// OFF-LABEL: define{{.*}} @two_roots(
// OFF: for.body.flexfat.fast:
// OFF-LABEL: define{{.*}} @small(
// OFF: for.body.flexfat.fast:
