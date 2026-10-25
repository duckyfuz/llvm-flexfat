// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte %s -o - | FileCheck %s --check-prefix=SHARED
// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-share-contained-geometry %s -o - | FileCheck %s --check-prefix=SEPARATE

// RUN: %clang -target aarch64-linux-gnu -O2 -S -emit-llvm -fsanitize=flexfat -fsanitize-flexfat-tbi=last-byte -fno-sanitize-flexfat-share-contained-geometry -fsanitize-flexfat-share-contained-geometry %s -o - | FileCheck %s --check-prefix=SHARED

extern void *malloc(unsigned long);

int contained(void) {
  volatile int *p = (volatile int *)malloc(64);
  return p[1] + p[2];
}

// SHARED-LABEL: define {{.*}} @contained(
// SHARED: %[[META:flexfat.metadata.*]] = {{inttoptr|select}}
// SHARED: load atomic i8, ptr %[[META]] {{acquire|monotonic}}
// SHARED: load volatile i32
// SHARED: load atomic i8, ptr %[[META]] {{acquire|monotonic}}
// SHARED: load volatile i32

// SEPARATE-LABEL: define {{.*}} @contained(
// SEPARATE: %flexfat.generation = load atomic i8, ptr {{.*}} {{acquire|monotonic}}
// SEPARATE: load volatile i32
// SEPARATE: %flexfat.generation{{[0-9]+}} = load atomic i8, ptr {{.*}} {{acquire|monotonic}}
// SEPARATE: load volatile i32
