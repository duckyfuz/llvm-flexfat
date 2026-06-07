// FlexFat: `-fsanitize=lowfat` is a deprecated alias for `-fsanitize=flexfat`.
// Verify three things the MSET differential harness (Unit 11) depends on:
//   (1) the deprecation warning has stable, exact text,
//   (2) compilation still SUCCEEDS -- the warning is non-fatal, and
//   (3) the alias lowers to exactly the flexfat configuration (same cc1 flags).
//
// REQUIRES: x86-registered-target

// (1)+(2): a real compile to an object file. The first RUN is a single command,
// so lit fails it if clang exits non-zero -- i.e. this asserts that compilation
// succeeds. `--implicit-check-not=error:` asserts the alias adds no errors.
// RUN: %clang --target=x86_64-unknown-linux-gnu -fsanitize=lowfat -c %s -o %t.o 2> %t.err
// RUN: FileCheck %s --check-prefix=WARN --implicit-check-not="error:" < %t.err
// WARN: warning: argument '-fsanitize=lowfat' is deprecated, use '-fsanitize=flexfat' instead

// (3): the alias lowers to the flexfat configuration -- cc1 sees `flexfat`, the
// forced large code model, and the BMI/LZCNT features (never a `lowfat` kind,
// since `lowfat` is not a SanitizerKind). This is what "behaves identically to
// -fsanitize=flexfat" means at the driver level.
// RUN: %clang --target=x86_64-unknown-linux-gnu -fsanitize=lowfat %s -### 2>&1 \
// RUN:   | FileCheck %s --check-prefix=LOWERS
// LOWERS: "-cc1"
// LOWERS-SAME: "-fsanitize=flexfat"
// LOWERS-SAME: "-mcmodel=large"
// LOWERS-SAME: "-target-feature" "+lzcnt"
// LOWERS-SAME: "-target-feature" "+bmi"
// LOWERS-SAME: "-target-feature" "+bmi2"

int main(void) { return 0; }
