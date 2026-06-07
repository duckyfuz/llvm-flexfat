// FlexFat Unit 13: the link-level classifier test. Proves the linker half
// (lowfat.ld INSERT AFTER, page-size override) that no IR test can.
//
// A regular `int g;` compiled with -fsanitize=flexfat must land in its
// region's [16 GiB, 24 GiB) global sub-range — lowfat_kind(&g) == "global",
// lowfat_base(&g) recovers the object's class base, and lowfat_size(&g)
// returns the class size (16, the smallest class).
//
// Unit 17: variant-specific assertions; gate to non-POW2.
// REQUIRES: flexfat-nonpow2
// RUN: %clang_flexfat_runtime -O2 %s -o %t
// RUN: %run %t
#include <stdio.h>
#include <stdlib.h>

#include <lowfat.h>

int g_mut = 7;            // mutable, eligible: lowfat_section_16
const int g_const = 11;   // const, eligible:   lowfat_section_const_16

int main(void) {
  if (!lowfat_is_ptr(&g_mut)) {
    fprintf(stderr, "g_mut=%p not lowfat\n", (void *)&g_mut);
    return 2;
  }
  if (!lowfat_is_global_ptr(&g_mut)) {
    fprintf(stderr, "g_mut=%p is lowfat but not global\n", (void *)&g_mut);
    return 3;
  }
  if (lowfat_size(&g_mut) != 16) {
    fprintf(stderr, "g_mut size=%zu, expected 16\n", lowfat_size(&g_mut));
    return 4;
  }
  if (lowfat_base(&g_mut) != (const void *)&g_mut) {
    fprintf(stderr, "g_mut base=%p, expected &g_mut=%p (16-aligned slot)\n",
            lowfat_base(&g_mut), (void *)&g_mut);
    return 5;
  }
  if (!lowfat_is_global_ptr(&g_const)) {
    fprintf(stderr, "g_const=%p not global\n", (void *)&g_const);
    return 6;
  }
  if (lowfat_size(&g_const) != 16) {
    fprintf(stderr, "g_const size=%zu, expected 16\n", lowfat_size(&g_const));
    return 7;
  }
  return 0;
}
