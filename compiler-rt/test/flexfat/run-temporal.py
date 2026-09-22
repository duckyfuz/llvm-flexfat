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
import hashlib

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
        required = [failure] if isinstance(failure, str) else failure
        assert p.returncode != 0 and all(s in p.stderr for s in required), (cmd, p.returncode, p.stderr)
    return p.stdout

def invalid_deallocation(exe, layout, position, operation):
    cmd = [str(exe), layout, position, operation]
    p = subprocess.run(cmd, text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=90)
    expected = re.search(r'EXPECT supplied=(0x[0-9a-f]+) expected=(0x[0-9a-f]+) base=(0x[0-9a-f]+)', p.stderr)
    diagnostic = re.search(
        r'supplied address = (0x[0-9a-f]+), expected allocation address = (0x[0-9a-f]+), slot base = (0x[0-9a-f]+)',
        p.stderr)
    op = 'free' if operation == 'free' else 'realloc'
    assert (p.returncode != 0 and expected and diagnostic
            and tuple(int(v, 16) for v in expected.groups()) ==
                tuple(int(v, 16) for v in diagnostic.groups())
            and 'FLEXFAT ERROR: invalid deallocation' in p.stderr
            and 'operation = ' + op + ',' in p.stderr
            and 'reason = not allocation start' in p.stderr), (cmd, p.returncode, p.stderr)

for build in args.builds:
    build = build.resolve()
    shared_temp = tempfile.TemporaryDirectory(prefix='flexfat-shared-')
    shared = Path(shared_temp.name)
    for deallocation in ['basic', 'exact']:
        cc = build / 'bin/clang'
        cxx = build / 'bin/clang++'
        common = ['-fsanitize=flexfat', '-fsanitize-flexfat-temporal=tagged',
                  '-fsanitize-flexfat-deallocation-check=' + deallocation]
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
        slots = 0
        for i, size in enumerate(sizes):
            start = region_base + (i << region_log)
            first = ((start + size - 1) // size) * size
            slots += (start + (1 << region_log) - first) // size
        page_size = os.sysconf('SC_PAGE_SIZE')
        def rounded(n):
            return ((n + page_size - 1) // page_size) * page_size
        temporal_bytes, offset_bytes = slots * 2, slots * (8 if deallocation == 'exact' else 0)
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
                'dead-interior-free': 'free', 'stale-interior-free': 'free',
                'dead-interior-realloc': 'realloc', 'stale-interior-realloc': 'realloc',
                'stale-realloc': 'realloc', 'realloc-zero': 'realloc',
                'zero-tag': 'read', 'never': 'read', 'thread': 'read',
                'memset': 'write', 'memcpy-src': 'read', 'memcpy-dst': 'write',
                'memmove': 'write', 'strdup': 'read', 'strndup': 'read', 'posix': 'write'}
            for flags, name in variants:
                run([cxx, *common, *flags, '-fno-builtin', '-pthread',
                     root/'TestCases/temporal/lifecycle.cpp', '-o', exe])
                for mode in ['', 'zero-length', 'realloc-failure', 'fallback', 'foreign']:
                    run([exe, *([mode] if mode else [])]); checks += 1
                for mode, operation in failures.items():
                    run([exe, mode], ['FLEXFAT ERROR: temporal violation',
                                     'operation = ' + operation]); checks += 1
                if custom:
                    run([exe, 'geometry'], 'unavailable (invalid slot geometry)'); checks += 1
                    run([exe, 'geometry-tail'], 'unavailable (invalid slot geometry)'); checks += 1
                exact = d/'exact'
                run([cxx, *common, *flags, '-fno-builtin', '-pthread',
                     root/'TestCases/temporal/exact_deallocation.cpp', '-o', exact])
                if deallocation == 'exact':
                    for layout in ['regular', 'aligned']:
                        run([exact, layout, 'valid']); checks += 1
                        positions = ['after']
                        # The custom 528-byte class is not 256-byte aligned. The test
                        # asserts a nonzero offset rather than silently skipping it.
                        if custom and layout == 'aligned':
                            positions += ['before', 'base']
                        if name == 'right-align' and layout == 'regular':
                            run([exact, layout, 'reuse-valid']); checks += 1
                            positions += ['before', 'base', 'reuse-old']
                        for position in positions:
                            for operation in ['free', 'realloc', 'realloc-zero', 'realloc-failure']:
                                invalid_deallocation(exact, layout, position, operation); checks += 1
                print(build.name, deallocation, name, 'passed', flush=True)

            same = d/'same'
            run([cxx, *common, '-O2', '-fno-builtin', root/'TestCases/temporal/same_slot.cpp', '-o', same])
            for operation in ['free', 'realloc']:
                run([same, operation], 'invalid deallocation' if deallocation == 'exact' else None)
                if deallocation == 'basic':
                    run([same, operation, 'stale'], 'temporal violation')
                checks += 1

            # Empty object retains the ABI contract even with section GC.
            empty = d/'empty.c'; empty.write_text('int main(void) { return 0; }\n')
            obj = shared/'empty.o'
            if deallocation == 'basic':
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
            runtime_name = 'flexfat_tbi_exact' if deallocation == 'exact' else 'flexfat_tbi'
            runtime = run([cc, '-print-file-name=libclang_rt.' + runtime_name + '.a']).strip()
            # The driver normally puts its runtime first. Explicit ordering places
            # this test hook ahead of the runtime's preinit entry.
            run([cc, startup, '-Wl,--whole-archive', runtime,
                 '-Wl,--no-whole-archive', '-lpthread', '-ldl', '-lrt', '-lm', '-o', exe])
            run([exe]); checks += 1
            run([cc, root/'Inputs/tbi-reentrant.c', '-fno-builtin', '-c', '-o', startup])
            run([cc, startup, *common, '-Wl,--wrap=dlsym', '-o', exe])
            run([exe]); checks += 1

            lib = shared/'lib.c'; lib.write_text('#include <stdlib.h>\nint dso_load(int *p) { return *p; }\nint *dso_alloc(void) { int *p=malloc(sizeof(int)); *p=9; return p; }\nvoid dso_free(void *p) { free(p); }\n')
            main = shared/'main.c'; main.write_text('#include <stdlib.h>\nextern int dso_load(int *); extern int *dso_alloc(void); extern void dso_free(void *);\nint main(int argc, char **argv) { int *q=dso_alloc(); if(*q!=9) return 1; free(q);\nint *p=malloc(sizeof(int)); *p=7; if(argc>1) { dso_free(p); return dso_load(p); }\nif(dso_load(p)!=7) return 1; dso_free(p); return 0; }\n')
            dso = shared/'libtest.so'
            main_obj = shared/'main.o'
            if deallocation == 'basic':
                run([cc, *common, '-O2', '-fno-builtin', '-fPIC', '-shared', lib, '-o', dso])
                run([cc, *common, '-O2', '-fno-builtin', '-c', main, '-o', main_obj])
                artifact_hashes = [hashlib.sha256(p.read_bytes()).hexdigest() for p in [obj, dso, main_obj]]
                ir = [run([cc, '-fsanitize=flexfat', '-fsanitize-flexfat-temporal=tagged',
                           '-fsanitize-flexfat-deallocation-check='+mode, '-O2', '-S', '-emit-llvm', lib, '-o', '-'])
                      for mode in ['basic', 'exact']]
                assert ir[0] == ir[1], 'deallocation mode changed instrumentation'
            assert artifact_hashes == [hashlib.sha256(p.read_bytes()).hexdigest() for p in [obj, dso, main_obj]]
            run([cc, *common, main_obj, '-L'+str(shared), '-ltest', '-Wl,-rpath,'+str(shared), '-Wl,--gc-sections', '-o', exe])
            run([exe]); run([exe, 'stale'], 'operation = read'); checks += 2
            assert ' T __flexfat_tbi_abi_v1' in run(['nm', exe])
            assert ' T __flexfat_tbi_abi_v1' not in run(['nm', dso])
            run([cc, '-fsanitize=flexfat', main_obj, '-L'+str(shared), '-ltest', '-o', d/'wrong-dso'], '__flexfat_tbi_abi_v1')
            run([cc, main_obj, '-L'+str(shared), '-ltest', '-Wl,--gc-sections', '-o', d/'missing-dso'], '__flexfat_tbi_abi_v1')

            timings = {}
            for tbi in [False, True]:
                run([cc, '-O2', '-fsanitize=flexfat', *(common[1:] if tbi else []),
                     root/'Inputs/tbi-benchmark.c', '-o', exe])
                for workload in ['allocations', 'accesses']:
                    samples = [run([exe, *(['access'] if workload == 'accesses' else [])]).split()
                               for _ in range(3)]
                    timings[('tagged-' + deallocation if tbi else 'ordinary')+'_'+workload] = {
                        'temporal': 'tagged' if tbi else 'off',
                        'deallocation': deallocation if tbi else 'basic',
                        'layout': 'custom' if custom else 'pow2',
                        'median_seconds': statistics.median(float(x[0]) for x in samples),
                        'peak_rss_kib': max(int(x[1]) for x in samples), 'samples': samples}
            results.append({'build': str(build), 'checks': checks,
                            'configuration': f'flexfat-{"custom" if custom else "pow2"}-temporal-tagged-deallocation-{deallocation}',
                            'layout': 'custom' if custom else 'pow2',
                            'temporal': 'tagged', 'deallocation': deallocation,
                            'metadata_bytes_per_slot': 10 if deallocation == 'exact' else 2,
                            'temporal_entry_bytes': temporal_bytes,
                            'offset_entry_bytes': offset_bytes,
                            'metadata_entry_bytes': temporal_bytes + offset_bytes,
                            'temporal_reservation_bytes': rounded(temporal_bytes),
                            'offset_reservation_bytes': rounded(offset_bytes),
                            'metadata_reservation_bytes': rounded(temporal_bytes) + rounded(offset_bytes),
                            'timings': timings})
            print(json.dumps(results[-1], indent=2), flush=True)
    shared_temp.cleanup()
if args.output:
    args.output.write_text(json.dumps(results, indent=2)+'\n')
