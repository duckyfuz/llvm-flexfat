// FlexFat Unit 7: the cold OOB error block must be emitted OUT OF LINE, after
// the fast-path return. FlexFat weights the error edge cold (1:2000000000) --
// intentionally inverted from the LowFat reference, which weights the error edge
// hot and relies on LLVM-4.0's noreturn-cold block-placement heuristic. LLVM 23's
// MachineBlockPlacement honors the explicit weight over that heuristic, so the
// reference's direction would put the cold block (and its call) on the hot
// fall-through. This test fails if a future backend change re-inverts placement.
//
// REQUIRES: x86-registered-target
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fsanitize=flexfat -O2 -S -o - %s \
// RUN:   | FileCheck %s

char get(char *q, int i) { return q[i]; }

// CHECK-LABEL: get:
// The conditional bounds-check branch and the fast-path return come first; the
// cold error block (calling lowfat_oob_error) is placed only afterwards.
// CHECK:      j{{[a-z]+}} .LBB
// CHECK:      ret
// CHECK:      lowfat_oob_error
