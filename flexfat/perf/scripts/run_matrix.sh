#!/usr/bin/env bash
# Build & time the {uninstrumented, flexfat-full, flexfat-hardened} matrix
# across the benchmark corpus. Reports raw wall-clock per run; analyze.py
# computes median + spread + overhead vs uninstrumented.
#
# Reference targets (llvm-lowfat/README.md, SPEC2006 @ -O2):
#   full      non-POW2: ~64%
#   hardened  non-POW2: ~9.8%   (-no-check-reads -no-check-escapes -no-check-fields)
#   hardened  POW2    : ~7.8%
# Our numbers are vs synthetic micro-benchmarks, not SPEC2006 — INDICATIVE.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PERF="$ROOT/perf"
CC_HOST="cc"
CC_FLEX="$ROOT/../build/bin/clang"
N=${N:-10}
RESULTS="$PERF/results/run_$(date -u +%Y%m%dT%H%M%SZ).tsv"

mkdir -p "$PERF/results" "$PERF/builds"

BENCHMARKS=(heap_churn array_sum linked_list memcpy_bulk opaque_access)

declare -A FLAGS
FLAGS[uninstrumented]=""
FLAGS[flexfat_full]="-fsanitize=flexfat"
FLAGS[flexfat_hardened]="-fsanitize=flexfat -mllvm -flexfat-no-check-reads -mllvm -flexfat-no-check-escapes -mllvm -flexfat-no-check-fields"

# 1) Build everything once.
for b in "${BENCHMARKS[@]}"; do
  for cfg in uninstrumented flexfat_full flexfat_hardened; do
    out="$PERF/builds/${b}__${cfg}"
    src="$PERF/benchmarks/${b}.c"
    if [ "$cfg" = "uninstrumented" ]; then
      "$CC_HOST" -O2 "$src" -o "$out" 2>/dev/null
    else
      # shellcheck disable=SC2086
      "$CC_FLEX" -O2 ${FLAGS[$cfg]} "$src" -o "$out" 2>/dev/null
    fi
  done
done
echo "[build OK]" >&2

# 2) Run matrix. Interleave configs per run to spread thermal/scheduling drift.
echo -e "bench\tconfig\trun\twall_seconds" > "$RESULTS"
for r in $(seq 1 "$N"); do
  for b in "${BENCHMARKS[@]}"; do
    for cfg in uninstrumented flexfat_full flexfat_hardened; do
      out="$PERF/builds/${b}__${cfg}"
      t=$( { /usr/bin/time -f "%e" "$out" > /dev/null; } 2>&1 )
      printf "%s\t%s\t%d\t%s\n" "$b" "$cfg" "$r" "$t" >> "$RESULTS"
    done
  done
  echo "[run $r/$N done]" >&2
done

echo "RESULTS=$RESULTS"
