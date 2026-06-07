// FlexFat driver wiring (Unit 6). -fsanitize=flexfat must:
//   (a) reach -cc1 (which is what enables the FlexFat pass in BackendUtil),
//   (b) force the large code model,
//   (c) error on non-x86_64 targets (the x86_64 gate),
//   (d) force the lzcnt/bmi/bmi2 feature set,
//   (e) link libclang_rt.flexfat (whole-archive) plus its system deps,
//   (f) accept `lowfat` as a deprecated alias.

// (a)+(b)+(d): forwarding to cc1 and the forced codegen flags.
// RUN: %clang --target=x86_64-unknown-linux-gnu -fsanitize=flexfat %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=CC1
// CC1: "-cc1"
// CC1-SAME: "-fsanitize=flexfat"
// CC1-SAME: "-mcmodel=large"
// CC1-SAME: "-target-feature" "+lzcnt"
// CC1-SAME: "-target-feature" "+bmi"
// CC1-SAME: "-target-feature" "+bmi2"

// (c): x86_64-only gate -> unsupported on other targets.
// RUN: not %clang --target=aarch64-unknown-linux-gnu -fsanitize=flexfat %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=GATE
// GATE: unsupported option '-fsanitize=flexfat' for target 'aarch64-unknown-linux-gnu'

// (e): runtime link line.
// RUN: %clang --target=x86_64-unknown-linux-gnu -fsanitize=flexfat -fuse-ld=ld \
// RUN:   -resource-dir=%S/Inputs/resource_dir --sysroot=%S/Inputs/basic_linux_tree \
// RUN:   %s -### 2>&1 | FileCheck %s --check-prefix=LINK
// LINK: "--whole-archive" "{{.*}}libclang_rt.flexfat{{.*}}" "--no-whole-archive"
// LINK: "-lpthread"
// LINK: "-ldl"

// (f): `lowfat` is a deprecated alias for `flexfat` -- it warns, but behaves
// exactly like -fsanitize=flexfat (same cc1 flag, same forced code model).
// RUN: %clang --target=x86_64-unknown-linux-gnu -fsanitize=lowfat %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ALIAS
// ALIAS: argument '-fsanitize=lowfat' is deprecated, use '-fsanitize=flexfat' instead
// ALIAS: "-cc1"
// ALIAS-SAME: "-fsanitize=flexfat"
// ALIAS-SAME: "-mcmodel=large"

int main(void) { return 0; }
