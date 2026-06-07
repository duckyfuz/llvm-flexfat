#!/usr/bin/env bash
# Capture per-benchmark instrumentation density via the FlexFat STATISTIC
# counters (NumChecks, NumElided, NumUnknownProducers). Run for the two
# instrumented configs only — uninstrumented has no pass.
#
# Output: one block per (bench, config) with the pass's `-stats` summary.

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PERF="$ROOT/perf"
CC_FLEX="$ROOT/../build/bin/clang"
OUT="$PERF/results/stats_$(date -u +%Y%m%dT%H%M%SZ).txt"

mkdir -p "$PERF/results"

BENCHMARKS=(heap_churn array_sum linked_list memcpy_bulk opaque_access)

declare -A FLAGS
FLAGS[flexfat_full]="-fsanitize=flexfat"
FLAGS[flexfat_hardened]="-fsanitize=flexfat -mllvm -flexfat-no-check-reads -mllvm -flexfat-no-check-escapes -mllvm -flexfat-no-check-fields"

: > "$OUT"
for b in "${BENCHMARKS[@]}"; do
  for cfg in flexfat_full flexfat_hardened; do
    src="$PERF/benchmarks/${b}.c"
    echo "=== $b :: $cfg ===" | tee -a "$OUT"
    # -mllvm -stats reports per-pass counters at the end of compilation.
    # Filter to FlexFat lines and counts.
    # shellcheck disable=SC2086
    "$CC_FLEX" -O2 ${FLAGS[$cfg]} -mllvm -stats -c "$src" -o /dev/null 2>&1 \
      | grep -iE "flexfat|^Number of " | tee -a "$OUT"
    echo "" | tee -a "$OUT"
  done
done

echo "STATS=$OUT"
