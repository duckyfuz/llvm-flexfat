#!/usr/bin/env python3
"""Compare shadow, full-width shadow, and in-slot TBI storage on one workload."""
import argparse
import json
import os
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
parser.add_argument('--runs', type=int, default=5)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
clang = args.build.resolve() / 'bin/clang'
source = r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static volatile unsigned sink;
int main(void) {
  const int count = 40000, rounds = 250;
  char **items = malloc((size_t)count * sizeof(*items));
  if (!items) return 2;
  for (int r = 0; r < rounds; ++r) {
    for (int i = 0; i < count; ++i) {
      items[i] = malloc(31);
      if (!items[i]) return 3;
      memset(items[i], i, 31);
    }
    for (int i = 0; i < count; ++i) {
      sink += (unsigned char)items[i][i % 31];
      free(items[i]);
    }
  }
  printf("%u\n", sink);
  free(items);
}
'''
variants = {
    'shadow': [],
    'shadow_full_width': ['-mllvm', '-flexfat-check-whole-access=true'],
    'last_byte': ['-mllvm', '-flexfat-tbi-storage=last-byte'],
    'prior_byte': ['-mllvm', '-flexfat-tbi-storage=prior-byte'],
}
base = ['-fsanitize=flexfat', '-fsanitize-flexfat-tbi', '-O2', '-fno-builtin']
results = {}
expected_stdout = None
with tempfile.TemporaryDirectory(prefix='flexfat-compare-') as tmp:
    tmp = Path(tmp)
    program = tmp / 'workload.c'
    program.write_text(source)
    for name, extra in variants.items():
        exe, ir = tmp / name, tmp / (name + '.ll')
        subprocess.run([clang, *base, *extra, program, '-o', exe], check=True)
        subprocess.run([clang, *base, *extra, '-S', '-emit-llvm', program,
                        '-o', ir], check=True)
        samples = []
        for run_index in range(args.runs):
            stdout_path, stderr_path = tmp / 'stdout', tmp / 'stderr'
            started = time.perf_counter()
            pid = os.fork()
            if pid == 0:
                os.dup2(os.open(stdout_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), 1)
                os.dup2(os.open(stderr_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600), 2)
                os.execv(str(exe), [str(exe)])
            _, status, usage = os.wait4(pid, 0)
            elapsed = time.perf_counter() - started
            stdout = stdout_path.read_text()
            stderr = stderr_path.read_text()
            assert os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0, (name, run_index, stderr)
            if expected_stdout is None:
                expected_stdout = stdout
            assert stdout == expected_stdout, (name, stdout)
            samples.append((elapsed, usage.ru_maxrss))
        assembly = ir.read_text()
        results[name] = {
            'seconds_median': statistics.median(x[0] for x in samples),
            'max_rss_kib_median': statistics.median(x[1] for x in samples),
            'ir_bytes': len(assembly),
            'spatial_report_calls': assembly.count('call void @__flexfat_report_oob') +
                                    assembly.count('call void @__flexfat_warn_oob'),
            'temporal_generation_loads': assembly.count('load atomic i8'),
            'samples': samples,
        }
    results['costs'] = {
        'mandatory_width_seconds': results['shadow_full_width']['seconds_median'] -
                                   results['shadow']['seconds_median'],
        'storage_seconds': results['last_byte']['seconds_median'] -
                           results['shadow_full_width']['seconds_median'],
        'prior_storage_seconds': results['prior_byte']['seconds_median'] -
                                 results['shadow_full_width']['seconds_median'],
        'mandatory_width_rss_kib': results['shadow_full_width']['max_rss_kib_median'] -
                                   results['shadow']['max_rss_kib_median'],
        'storage_rss_kib': results['last_byte']['max_rss_kib_median'] -
                           results['shadow_full_width']['max_rss_kib_median'],
        'prior_storage_rss_kib': results['prior_byte']['max_rss_kib_median'] -
                                 results['shadow_full_width']['max_rss_kib_median'],
    }
print(json.dumps(results, indent=2))
if args.output:
    args.output.write_text(json.dumps(results, indent=2) + '\n')
