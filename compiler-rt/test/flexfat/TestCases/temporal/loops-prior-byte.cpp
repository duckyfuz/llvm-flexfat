// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -mllvm -flexfat-version-tbi-loops=true -fsanitize-flexfat-tbi-storage=prior-byte -O2 -fno-vectorize -fno-slp-vectorize -fno-unroll-loops %s -o %t && %t
// RUN: not %t quad-call 2>&1 | FileCheck %s
// RUN: not %t quad-cross 2>&1 | FileCheck %s --check-prefix=CROSS
// RUN: not %t quad-overflow 2>&1 | FileCheck %s --check-prefix=CROSS
// CHECK: temporal violation
// CHECK: generation mismatch
// CROSS: out-of-bounds error detected

#include "loops.cpp"
