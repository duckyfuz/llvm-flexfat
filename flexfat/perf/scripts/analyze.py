#!/usr/bin/env python3
"""Summarize a perf TSV: median wall-clock per (bench, config) plus IQR
spread and overhead vs uninstrumented. Reads the TSV path from argv[1] or
the newest in perf/results/.
"""
import csv
import statistics
import sys
from pathlib import Path

def load(path):
    bench_cfg = {}
    with open(path) as f:
        reader = csv.DictReader(f, delimiter="\t")
        for row in reader:
            key = (row["bench"], row["config"])
            bench_cfg.setdefault(key, []).append(float(row["wall_seconds"]))
    return bench_cfg

def quantiles(xs):
    xs_sorted = sorted(xs)
    n = len(xs_sorted)
    def q(p):
        idx = (n - 1) * p
        lo, hi = int(idx), min(int(idx) + 1, n - 1)
        return xs_sorted[lo] + (xs_sorted[hi] - xs_sorted[lo]) * (idx - lo)
    return q(0.25), q(0.50), q(0.75)

def main():
    if len(sys.argv) > 1:
        path = Path(sys.argv[1])
    else:
        results = Path(__file__).parent.parent / "results"
        runs = sorted(results.glob("run_*.tsv"))
        if not runs:
            print("no run_*.tsv in", results, file=sys.stderr); sys.exit(1)
        path = runs[-1]
    print(f"# source: {path}")

    data = load(path)
    benches = sorted({b for (b, _) in data})
    configs = ["uninstrumented", "flexfat_full", "flexfat_hardened"]

    print(f"# N runs per cell: {len(next(iter(data.values())))}")
    print(f"# spread shown as IQR (q75 - q25) seconds")
    print()
    hdr = f"{'bench':<14}{'config':<22}{'median_s':>10}{'iqr_s':>10}{'overhead_vs_uninstr':>22}"
    print(hdr)
    print("-" * len(hdr))
    for b in benches:
        uninstr_med = statistics.median(data[(b, "uninstrumented")])
        for cfg in configs:
            xs = data[(b, cfg)]
            q25, med, q75 = quantiles(xs)
            iqr = q75 - q25
            if cfg == "uninstrumented":
                overhead = "-"
            else:
                overhead = f"{(med - uninstr_med) / uninstr_med * 100:+.1f}%"
            print(f"{b:<14}{cfg:<22}{med:>10.3f}{iqr:>10.3f}{overhead:>22}")
        print()

    # Geometric-mean style summary across benchmarks per config.
    print("# Aggregate (mean of per-benchmark overhead percentages):")
    for cfg in configs[1:]:
        ovs = []
        for b in benches:
            uninstr_med = statistics.median(data[(b, "uninstrumented")])
            cfg_med = statistics.median(data[(b, cfg)])
            ovs.append((cfg_med - uninstr_med) / uninstr_med * 100)
        print(f"  {cfg:<22} mean overhead: {sum(ovs)/len(ovs):+.1f}%")

if __name__ == "__main__":
    main()
