// REQUIRES: flexfat-tbi
// UNSUPPORTED: flexfat-custom-config
// RUN: %clangxx_flexfat_tbi -fsanitize-flexfat-tbi=shadow -fno-builtin -O2 -DTEST_TBI %s -pthread -o %t && %run %t
// RUN: %clangxx_flexfat_tbi -fsanitize-flexfat-tbi=packed-shadow -fno-builtin -O2 -DTEST_TBI %s -pthread -o %t && %run %t
#include "../core/lowfat_allocator.cpp"
