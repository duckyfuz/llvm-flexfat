// RUN: not %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-recover=flexfat -fsyntax-only %s 2>&1 | FileCheck %s --check-prefix=UNRECOVERABLE
// RUN: %clang -target aarch64-linux-gnu -fsanitize=flexfat -fsanitize-recover=all -### -c %s 2>&1 | FileCheck %s --check-prefix=FATAL
// UNRECOVERABLE: unsupported argument 'flexfat' to option '-fsanitize-recover='
// FATAL-NOT: "-fsanitize-recover=
// FATAL: "-fsanitize=flexfat"
// FATAL-NOT: "-fsanitize-recover=

int value;
