// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-share-contained-geometry -### -c %s 2>&1 | FileCheck %s --check-prefix=FORWARD
// FORWARD: "-fno-sanitize-flexfat-share-contained-geometry"
// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte %s -o - | FileCheck %s --check-prefix=COMMON
// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-share-contained-geometry %s -o - | FileCheck %s --check-prefix=COMMON

// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-share-contained-geometry -fsanitize-flexfat-share-contained-geometry %s -o - | FileCheck %s --check-prefix=COMMON

// Spatial proofs can share geometry even when optional contained sharing is disabled.
extern void *malloc(unsigned long);

int contained(void) {
  volatile int *p = (volatile int *)malloc(64);
  return p[1] + p[2];
}

// COMMON-LABEL: define {{.*}} @contained(
// COMMON: %[[META:flexfat.metadata.*]] = {{inttoptr|select}}
// COMMON: load i8, ptr %[[META]]
// COMMON: load volatile i32
// COMMON-NOT: load i8
// COMMON: load volatile i32
// COMMON-NOT: load i8
// COMMON: ret i32
