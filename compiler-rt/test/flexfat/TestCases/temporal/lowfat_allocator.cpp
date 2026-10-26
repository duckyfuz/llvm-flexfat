// REQUIRES: flexfat-tbi
// RUN: %clangxx_flexfat_tbi -fno-builtin -O2 -DTEST_TBI -DTEST_TBI_IN_SLOT %s -pthread -o %t && %run %t
// RUN: %clangxx_flexfat_tbi -mllvm -flexfat-tbi-storage=last-byte -fno-builtin -O2 -DTEST_TBI -DTEST_TBI_IN_SLOT %s -pthread -o %t && %run %t
// RUN: %clangxx_flexfat_tbi -mllvm -flexfat-tbi-storage=prior-byte -fno-builtin -O2 -DTEST_TBI -DTEST_TBI_IN_SLOT %s -pthread -o %t && %run %t
#include "../core/lowfat_allocator.cpp"
