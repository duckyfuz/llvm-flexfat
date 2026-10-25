// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte %s -o - | FileCheck %s --check-prefix=SHARED
// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-share-contained-geometry %s -o - | FileCheck %s --check-prefix=SEPARATE

// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-share-contained-geometry -fsanitize-flexfat-share-contained-geometry %s -o - | FileCheck %s --check-prefix=SHARED

// Cleanup may reuse generation observations even with geometry sharing
// disabled. The runs also cover forwarding and last-option-wins behavior.
extern void *malloc(unsigned long);

int contained(void) {
  volatile int *p = (volatile int *)malloc(64);
  return p[1] + p[2];
}

// SHARED-LABEL: define {{.*}} @contained(
// SHARED: %[[META:flexfat.metadata.*]] = {{inttoptr|select}}
// SHARED: load i8, ptr %[[META]]
// SHARED: load volatile i32
// SHARED-NOT: load i8
// SHARED: load volatile i32
// SHARED-NOT: load i8
// SHARED: ret i32

// SEPARATE-LABEL: define {{.*}} @contained(
// SEPARATE: %flexfat.generation = load i8, ptr
// SEPARATE: load volatile i32
// SEPARATE: load volatile i32
// SEPARATE: ret i32
