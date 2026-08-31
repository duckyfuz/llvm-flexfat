//===-- flexfat_config_gen.c - FlexFat Size Class Config Generator
//--------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Standalone C tool (no LLVM dependencies) that reads a sizes.cfg file and
// emits flexfat_config_generated.h containing:
//
//   - kFlexFatGenSizes[]   : actual object sizes for each region index
//   - kFlexFatGenMagics[]  : precomputed ceil(2^64/S) reciprocals for all sizes
//   - flexfat_size_to_class(): binary-search mapping from alloc size → region
//   index
//
// sizes.cfg format:
//   - One size per line (plain integer)
//   - Sizes must be multiples of 16
//   - First size must be 16
//   - Sizes must be in strictly ascending order
//   - Maximum size ≤ one quarter of the 256 GiB region size
//
// Usage:
//   flexfat_config_gen <sizes.cfg> <output_header>
//
// The precision checker validates that the fixed-point formula
//   base = (ptr * magic >> 64) * S
// correctly identifies the start of every object within its absolute region.
// A quotient correction makes the ceil-reciprocal result exact; configurations
// for which that property cannot be proved are rejected.
//
//===----------------------------------------------------------------------===//

#include <assert.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --------------------------------------------------------------------------
// Configuration constants (must stay in sync with flexfat_config.h)
// --------------------------------------------------------------------------

// 64 LowFat source classes need a 256 GiB region so the largest 64 GiB class
// remains no greater than one quarter of a region.
#define REGION_SIZE_LOG 38
#define REGION_SIZE ((uint64_t)1 << REGION_SIZE_LOG)
#define REGION_BASE UINT64_C(0x100000000000)
#define TABLES_BASE UINT64_C(0x300000000000)
#define TABLES_SIZE UINT64_C(0x04000000)
#define MAX_SIZE_CLASSES 256

// Minimum alignment / granularity
#define MIN_SIZE 16

// --------------------------------------------------------------------------
// __int128 helpers (standard C99/C11 with GCC/Clang extension)
// --------------------------------------------------------------------------

typedef unsigned __int128 u128;

// Compute ceil(2^64 / S) using 128-bit arithmetic.
// This is the magic number M used by custom-config base recovery:
//   floor(P / S) ~= (P * M) >> 64
// The precision checker below verifies the usable range inside a region.
static uint64_t compute_magic(uint64_t S) {
  if (S == 0)
    return 0;
  u128 two64 = (u128)1 << 64;
  uint64_t q = (uint64_t)(two64 / S);
  uint64_t r = (uint64_t)(two64 % S);
  return q + (r != 0 ? 1 : 0); // ceil division
}

// Returns 1 if n is an exact power of two, 0 otherwise.
static int is_pow2(uint64_t n) { return n != 0 && (n & (n - 1)) == 0; }

static uint64_t recover_base(uint64_t ptr, uint64_t size, uint64_t magic) {
  uint64_t quotient = (uint64_t)(((u128)ptr * magic) >> 64);
  u128 product = (u128)quotient * size;
  if (product > ptr)
    --quotient;
  return quotient * size;
}

static int check_point(uint64_t ptr, uint64_t size, uint64_t magic) {
  return recover_base(ptr, size, magic) == (ptr / size) * size;
}

// M=ceil(2^64/S) never underestimates floor(P/S).  If M*S=2^64+d,
// the uncorrected estimate is at most one too high when
//   floor(P/S)*d + (S-1)*M < 2*2^64.
// The single product comparison in recover_base then makes it exact.  Check
// that inequality for the largest absolute address in the class region, and
// also exercise both sides of the first and last quotient boundaries.
static int prove_reciprocal(uint64_t region_start, uint64_t size,
                            uint64_t magic) {
  uint64_t region_end = region_start + REGION_SIZE;
  u128 two64 = (u128)1 << 64;
  u128 d = (u128)magic * size - two64;
  uint64_t max_ptr = region_end - 1;
  u128 upper = (u128)(max_ptr / size) * d + (u128)(size - 1) * magic;
  if (upper >= (two64 << 1))
    return 0;

  uint64_t points[10];
  int count = 0;
  points[count++] = region_start;
  points[count++] = max_ptr;
  uint64_t first_boundary = ((region_start + size - 1) / size) * size;
  uint64_t last_boundary = (max_ptr / size) * size;
  uint64_t boundaries[2] = {first_boundary, last_boundary};
  for (int i = 0; i < 2; ++i) {
    uint64_t boundary = boundaries[i];
    if (boundary > region_start)
      points[count++] = boundary - 1;
    if (boundary >= region_start && boundary < region_end)
      points[count++] = boundary;
    if (boundary < max_ptr)
      points[count++] = boundary + 1;
  }
  for (int i = 0; i < count; ++i)
    if (!check_point(points[i], size, magic))
      return 0;
  return 1;
}

// --------------------------------------------------------------------------
// Main
// --------------------------------------------------------------------------

int main(int argc, char *argv[]) {
  if (argc != 3) {
    fprintf(stderr, "Usage: %s <sizes.cfg> <output_header>\n", argv[0]);
    return 1;
  }

  const char *cfg_path = argv[1];
  const char *out_path = argv[2];

  // ---- Parse sizes.cfg ----
  FILE *cfg = fopen(cfg_path, "r");
  if (!cfg) {
    fprintf(stderr, "Error: cannot open '%s'\n", cfg_path);
    return 1;
  }

  uint64_t sizes[MAX_SIZE_CLASSES];
  int num_sizes = 0;
  char line[256];

  while (fgets(line, sizeof(line), cfg)) {
    // Skip blank lines and comments
    char *p = line;
    while (*p == ' ' || *p == '\t')
      p++;
    if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
      continue;

    uint64_t s = (uint64_t)strtoull(p, NULL, 10);
    if (s == 0)
      continue;

    if (num_sizes >= MAX_SIZE_CLASSES) {
      fprintf(stderr, "Error: too many size classes (max %d)\n",
              MAX_SIZE_CLASSES);
      fclose(cfg);
      return 1;
    }
    sizes[num_sizes++] = s;
  }
  fclose(cfg);

  if (num_sizes == 0) {
    fprintf(stderr, "Error: no valid sizes found in '%s'\n", cfg_path);
    return 1;
  }

  // ---- Validate ----
  if (sizes[0] != MIN_SIZE) {
    fprintf(stderr, "Error: first size must be %d, got %" PRIu64 "\n", MIN_SIZE,
            sizes[0]);
    return 1;
  }
  for (int i = 0; i < num_sizes; i++) {
    if (sizes[i] % MIN_SIZE != 0) {
      fprintf(stderr, "Error: size %" PRIu64 " is not a multiple of %d\n",
              sizes[i], MIN_SIZE);
      return 1;
    }
    if (sizes[i] > REGION_SIZE / 4) {
      fprintf(stderr,
              "Error: size %" PRIu64
              " exceeds one quarter of region size %" PRIu64 "\n",
              sizes[i], REGION_SIZE);
      return 1;
    }
    if (i > 0 && sizes[i] <= sizes[i - 1]) {
      fprintf(stderr,
              "Error: sizes must be strictly ascending; "
              "sizes[%d]=%" PRIu64 " <= sizes[%d]=%" PRIu64 "\n",
              i, sizes[i], i - 1, sizes[i - 1]);
      return 1;
    }
  }

  // ---- Compute tables ----
  uint64_t magics[MAX_SIZE_CLASSES];
  for (int i = 0; i < num_sizes; i++) {
    uint64_t S = sizes[i];
    uint64_t region_start = REGION_BASE + (uint64_t)i * REGION_SIZE;
    magics[i] = compute_magic(S);
    if (!prove_reciprocal(region_start, S, magics[i])) {
      fprintf(stderr,
              "Error: reciprocal proof failed for class %d, size %" PRIu64
              ", region [0x%" PRIx64 ",0x%" PRIx64 ")\n",
              i, S, region_start, region_start + REGION_SIZE);
      return 1;
    }
  }

  u128 managed_end = (u128)REGION_BASE + (u128)num_sizes * REGION_SIZE;
  if (managed_end > TABLES_BASE) {
    fprintf(stderr, "Error: managed regions overlap fixed metadata tables\n");
    return 1;
  }
  if ((u128)TABLES_BASE + TABLES_SIZE > ((u128)1 << 48)) {
    fprintf(stderr,
            "Error: fixed metadata is outside the 48-bit address space\n");
    return 1;
  }

  // ---- Open output ----
  FILE *out = fopen(out_path, "w");
  if (!out) {
    fprintf(stderr, "Error: cannot open output '%s'\n", out_path);
    return 1;
  }

  // ---- Emit header ----
  fprintf(out,
          "//===-- flexfat_config_generated.h - Auto-generated FlexFat config "
          "---------===//\n"
          "//\n"
          "// AUTO-GENERATED by flexfat_config_gen. DO NOT EDIT.\n"
          "// Source: %s\n"
          "//\n"
          "//"
          "===-----------------------------------------------------------------"
          "-----===//\n"
          "\n"
          "#pragma once\n"
          "#ifndef LF_CONFIG_GENERATED_H\n"
          "#define LF_CONFIG_GENERATED_H\n"
          "\n"
          "#include <stdint.h>\n"
          "\n"
          "// Fixed layout shared by the compiler pass and runtime.\n"
          "#define FLEXFAT_CUSTOM_CONFIG       1\n"
          "#define FLEXFAT_REGION_BASE         UINT64_C(0x100000000000)\n"
          "#define FLEXFAT_REGION_SIZE_LOG     38\n"
          "#define FLEXFAT_REGION_SIZE         (UINT64_C(1) << "
          "FLEXFAT_REGION_SIZE_LOG)\n"
          "#define FLEXFAT_TABLES_BASE         UINT64_C(0x300000000000)\n"
          "#define FLEXFAT_NUM_SIZE_CLASSES    %d\n"
          "#define FLEXFAT_MAX_SIZE            UINT64_C(%" PRIu64 ")\n"
          "#if defined(__cplusplus)\n"
          "static_assert(FLEXFAT_REGION_BASE + FLEXFAT_NUM_SIZE_CLASSES * "
          "FLEXFAT_REGION_SIZE <= FLEXFAT_TABLES_BASE, \"managed regions "
          "overlap metadata\");\n"
          "static_assert(FLEXFAT_MAX_SIZE <= FLEXFAT_REGION_SIZE / 4, "
          "\"largest class exceeds region quarter\");\n"
          "#else\n"
          "_Static_assert(FLEXFAT_REGION_BASE + FLEXFAT_NUM_SIZE_CLASSES * "
          "FLEXFAT_REGION_SIZE <= FLEXFAT_TABLES_BASE, \"managed regions "
          "overlap metadata\");\n"
          "_Static_assert(FLEXFAT_MAX_SIZE <= FLEXFAT_REGION_SIZE / 4, "
          "\"largest class exceeds region quarter\");\n"
          "#endif\n"
          "\n",
          cfg_path, num_sizes, sizes[num_sizes - 1]);

  // kFlexFatGenSizes
  fprintf(
      out,
      "// Allocation size for each region index.\n"
      "static const uint64_t kFlexFatGenSizes[FLEXFAT_NUM_SIZE_CLASSES] = {\n"
      "    /* idx: size */\n");
  for (int i = 0; i < num_sizes; i++) {
    fprintf(out, "    /* %3d */ UINT64_C(%" PRIu64 ")%s\n", i, sizes[i],
            (i < num_sizes - 1) ? "," : "");
  }
  fprintf(out, "};\n\n");

  // kFlexFatGenMagics
  fprintf(
      out,
      "// M = ceil(2^64 / S); users correct a one-high quotient.\n"
      "static const uint64_t kFlexFatGenMagics[FLEXFAT_NUM_SIZE_CLASSES] = {\n"
      "    /* idx: magic */\n");
  for (int i = 0; i < num_sizes; i++) {
    fprintf(out,
            "    /* %3d */ UINT64_C(0x%016" PRIx64 ")%s  // size=%" PRIu64
            "%s\n",
            i, magics[i], (i < num_sizes - 1) ? "," : " ", sizes[i],
            is_pow2(sizes[i]) ? " (POW2)" : "");
  }
  fprintf(out, "};\n\n");

  // flexfat_size_to_class: binary search — replaces SizeClassIndex()
  fprintf(
      out,
      "// Maps a requested allocation size to the smallest region index whose\n"
      "// effective size >= requested size.  Replaces SizeClassIndex() when\n"
      "// FLEXFAT_CUSTOM_CONFIG is active.\n"
      "// Returns FLEXFAT_NUM_SIZE_CLASSES if size exceeds all size classes.\n"
      "static inline uint64_t flexfat_size_to_class(uint64_t size) {\n"
      "    // Binary search over kFlexFatGenSizes[]\n"
      "    if (size == 0) size = 1;\n"
      "    uint64_t lo = 0, hi = FLEXFAT_NUM_SIZE_CLASSES;\n"
      "    while (lo < hi) {\n"
      "        uint64_t mid = lo + (hi - lo) / 2;\n"
      "        if (kFlexFatGenSizes[mid] < size)\n"
      "            lo = mid + 1;\n"
      "        else\n"
      "            hi = mid;\n"
      "    }\n"
      "    return lo;  // lo == FLEXFAT_NUM_SIZE_CLASSES means no fit\n"
      "}\n"
      "\n"
      "#endif  // LF_CONFIG_GENERATED_H\n");

  fclose(out);

  fprintf(stderr, "flexfat_config_gen: generated '%s' with %d size classes\n",
          out_path, num_sizes);

  // Print summary table to stdout
  printf("Size Class Table:\n");
  printf("  %-5s  %-10s  %-6s  %-20s  %-20s\n", "Idx", "ReqSize", "POW2?",
         "ClassSize", "Magic");
  printf(
      "  %s\n",
      "---------------------------------------------------------------------");
  for (int i = 0; i < num_sizes; i++) {
    printf("  %-5d  %-10" PRIu64 "  %-6s  %-20" PRIu64 "  %#-20" PRIx64 "\n", i,
           sizes[i], is_pow2(sizes[i]) ? "yes" : "no", sizes[i], magics[i]);
  }

  return 0;
}
