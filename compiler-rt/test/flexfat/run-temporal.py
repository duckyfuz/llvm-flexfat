#!/usr/bin/env python3
"""Native acceptance and timing harness. Pass one or more matching build dirs."""
import argparse
import json
import os
import re
from pathlib import Path
import statistics
import subprocess
import tempfile

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('builds', nargs='+', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
results = []

def run(cmd, failure=None):
    p = subprocess.run([str(c) for c in cmd], text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=90)
    if failure is None:
        assert p.returncode == 0, (cmd, p.returncode, p.stdout, p.stderr)
    else:
        assert p.returncode != 0 and failure in p.stderr, (cmd, p.returncode, p.stderr)
    return p.stdout

for build in args.builds:
    build = build.resolve()
    cc = build / 'bin/clang'
    cxx = build / 'bin/clang++'
    common = ['-fsanitize=flexfat', '-fsanitize-flexfat-tbi']
    # CMake permits STRING as the cache type as well.
    custom = any(line.startswith('FLEXFAT_SIZES_CFG:') and line.split('=',1)[1]
                 for line in (build/'CMakeCache.txt').read_text().splitlines())
    if custom:
        header = (build/'lib/Transforms/Instrumentation/flexfat_config_generated.h').read_text()
        table = header.split('kFlexFatGenSizes[', 1)[1].split('};', 1)[0]
        sizes = [int(n) for n in re.findall(r'UINT64_C\((\d+)\)', table)]
        region_log = int(re.search(r'#define FLEXFAT_REGION_SIZE_LOG\s+(\d+)', header)[1])
        region_base = int(re.search(r'#define FLEXFAT_REGION_BASE\s+UINT64_C\((0x[0-9a-fA-F]+)\)', header)[1], 16)
    else:
        sizes = [1 << n for n in range(4, 31)]
        region_log, region_base = 32, 0x100000000000
    metadata_bytes = 0
    for i, size in enumerate(sizes):
        start = region_base + (i << region_log)
        first = ((start + size - 1) // size) * size
        metadata_bytes += ((start + (1 << region_log) - first) // size) * 2
    checks = 0
    with tempfile.TemporaryDirectory(prefix='flexfat-tbi-') as directory:
        d = Path(directory)
        exe = d/'test'
        variants = [(['-O0'], 'O0'), (['-O2'], 'O2'),
                    (['-O2', '-mllvm', '-flexfat-mode=safe'], 'safe'),
                    (['-O2', '-mllvm', '-flexfat-mode=right-align'], 'right-align'),
                    (['-O2', '-mllvm', '-flexfat-placement=optimizer-early'], 'early'),
                    (['-O2', '-mllvm', '-flexfat-placement=optimizer-last'], 'last'),
                    (['-O2', '-fsanitize-recover=flexfat'], 'recover')]
        failures = {
            'read': 'read', 'write': 'write', 'reuse': 'read',
            'double-free': 'free', 'stale-free': 'free', 'realloc': 'realloc',
            'stale-realloc': 'realloc', 'realloc-zero': 'realloc',
            'zero-tag': 'read', 'never': 'read', 'thread': 'read',
            'memset': 'write', 'memcpy-src': 'read', 'memcpy-dst': 'write',
            'memmove': 'write', 'strdup': 'read', 'strndup': 'read', 'posix': 'write'}
        for flags, name in variants:
            run([cxx, *common, *flags, '-fno-builtin', '-pthread',
                 root/'TestCases/temporal/lifecycle.cpp', '-o', exe])
            for mode in ['', 'zero-length', 'realloc-failure', 'fallback']:
                run([exe, *([mode] if mode else [])]); checks += 1
            for mode, operation in failures.items():
                run([exe, mode], 'operation = ' + operation); checks += 1
            if custom:
                run([exe, 'geometry'], 'unavailable (invalid slot geometry)'); checks += 1
                run([exe, 'geometry-tail'], 'unavailable (invalid slot geometry)'); checks += 1
            print(build.name, name, 'passed', flush=True)

        # Empty object retains the ABI contract even with section GC.
        empty = d/'empty.c'; empty.write_text('int main(void) { return 0; }\n')
        obj = d/'empty.o'
        run([cc, *common, '-O2', '-ffunction-sections', '-fdata-sections', '-c', empty, '-o', obj])
        run([cc, *common, obj, '-Wl,--gc-sections', '-o', exe]); run([exe])
        run([cc, '-fsanitize=flexfat', obj, '-Wl,--gc-sections', '-o', d/'wrong'], '__flexfat_tbi_abi_v1')
        run([cc, obj, '-Wl,--gc-sections', '-o', d/'missing'], '__flexfat_tbi_abi_v1')
        checks += 3
        launcher = d/'launcher'
        run([cc, root/'Inputs/tbi-init-failure.c', '-o', launcher])
        run([launcher, exe], 'initialization failed: PR_SET_TAGGED_ADDR_CTRL, errno=1'); checks += 1

        # An uninstrumented preinit hook precedes runtime initialization and
        # forces allocations/memory operations through initialization guards.
        startup = d/'startup.o'
        run([cc, '-fno-builtin', '-c', root/'Inputs/tbi-startup.c', '-o', startup])
        runtime = run([cc, '-print-file-name=libclang_rt.flexfat_tbi.a']).strip()
        # The driver normally puts its runtime first. Explicit ordering places
        # this test hook ahead of the runtime's preinit entry.
        run([cc, startup, '-Wl,--whole-archive', runtime,
             '-Wl,--no-whole-archive', '-lpthread', '-ldl', '-lrt', '-lm', '-o', exe])
        run([exe]); checks += 1
        run([cc, root/'Inputs/tbi-reentrant.c', '-fno-builtin', '-c', '-o', startup])
        run([cc, startup, *common, '-Wl,--wrap=dlsym', '-o', exe])
        run([exe]); checks += 1

        lib = d/'lib.c'; lib.write_text('int dso_load(int *p) { return *p; }\n')
        main = d/'main.c'; main.write_text('''#include <stdlib.h>
extern int dso_load(int *);
int main(int argc, char **argv) { int *p=malloc(sizeof(int)); *p=7;
if(argc>1) free(p); return dso_load(p)==7 ? 0 : 1; }
''')
        run([cc, *common, '-O2', '-fPIC', '-shared', lib, '-o', d/'libtest.so'])
        run([cc, *common, '-O2', main, '-L'+str(d), '-ltest', '-Wl,-rpath,'+str(d), '-o', exe])
        run([exe]); run([exe, 'stale'], 'operation = read'); checks += 2

        timings = {}
        for tbi in [False, True]:
            run([cc, '-O2', '-fsanitize=flexfat', *(['-fsanitize-flexfat-tbi'] if tbi else []),
                 root/'Inputs/tbi-benchmark.c', '-o', exe])
            for workload in ['allocations', 'accesses']:
                samples = [run([exe, *(['access'] if workload == 'accesses' else [])]).split()
                           for _ in range(3)]
                timings[('tbi' if tbi else 'ordinary')+'_'+workload] = {
                    'median_seconds': statistics.median(float(x[0]) for x in samples),
                    'peak_rss_kib': max(int(x[1]) for x in samples), 'samples': samples}
        results.append({'build': str(build), 'checks': checks,
                        'metadata_entry_bytes': metadata_bytes,
                        'metadata_reservation_bytes': ((metadata_bytes + os.sysconf('SC_PAGE_SIZE') - 1)
                                                       // os.sysconf('SC_PAGE_SIZE')) * os.sysconf('SC_PAGE_SIZE'),
                        'timings': timings})
        print(json.dumps(results[-1], indent=2), flush=True)
if args.output:
    args.output.write_text(json.dumps(results, indent=2)+'\n')
