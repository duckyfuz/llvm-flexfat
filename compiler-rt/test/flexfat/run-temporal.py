#!/usr/bin/env python3
"""Native acceptance harness. Pass one or more matching build dirs."""
import argparse
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('builds', nargs='+', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--skip-timings', action='store_true',
                    help='retained for compatibility; this harness now runs correctness checks only')
parser.add_argument('--storage', choices=['shadow', 'last-byte', 'prior-byte'],
                    default='shadow')
args = parser.parse_args()
results = []

def run(cmd, failure=None):
    p = subprocess.run([str(c) for c in cmd], text=True, stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, timeout=90)
    if failure is None:
        assert p.returncode == 0, (cmd, p.returncode, p.stdout, p.stderr)
    else:
        expected = [failure] if isinstance(failure, str) else failure
        assert p.returncode != 0 and all(s in p.stderr for s in expected), (cmd, p.returncode, p.stderr)
    return p.stdout

for build in args.builds:
    build = build.resolve()
    cc = build / 'bin/clang'
    cxx = build / 'bin/clang++'
    common = ['-fsanitize=flexfat', '-fsanitize-flexfat-tbi',
              '-mllvm', '-flexfat-tbi-storage=' + args.storage]
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
    abi = (('__flexfat_tbi_abi_prior_byte_custom_v2' if custom else
            '__flexfat_tbi_abi_prior_byte_pow2_v2') if args.storage == 'prior-byte' else
           ('__flexfat_tbi_abi_last_byte_custom_v1' if custom else
            '__flexfat_tbi_abi_last_byte_pow2_v1') if args.storage == 'last-byte' else
           '__flexfat_tbi_abi_v3' if custom else '__flexfat_tbi_abi_v4')
    metadata_bytes = 0
    for i, size in enumerate(sizes):
        start = region_base + (i << region_log)
        first = start // size
        metadata_bytes += ((start + (1 << region_log) - 1) // size - first + 1
                           if custom else (1 << region_log) // size)
    page = os.sysconf('SC_PAGE_SIZE')
    reservation_bytes = (0 if args.storage != 'shadow' else
                         ((metadata_bytes + page - 1) // page) * page if custom
                         else page + sum((((1 << region_log) // size + page - 1) // page) * page
                                         for size in sizes))
    if args.storage != 'shadow':
        metadata_bytes = 0
    checks = 0
    with tempfile.TemporaryDirectory(prefix='flexfat-tbi-') as directory:
        d = Path(directory)
        exe = d/'test'
        variants = [(['-O0'], 'O0'), (['-O2'], 'O2'),
                    (['-O2', '-mllvm', '-flexfat-mode=safe'], 'safe'),
                    (['-O2', '-mllvm', '-flexfat-alignment=right'], 'right-align'),
                    (['-O2', '-mllvm', '-flexfat-mode=safe', '-mllvm', '-flexfat-alignment=right'], 'safe-right'),
                    (['-O2', '-mllvm', '-flexfat-recover=true'], 'recover')]
        failures = {
            'read': 'read', 'write': 'write', 'reuse': 'read', 'wrap-free': 'read',
            'double-free': 'free', 'stale-free': 'free', 'realloc': 'realloc',
            'stale-realloc': 'realloc', 'realloc-zero': 'realloc',
            'zero-tag': 'read', 'never': 'read', 'never-zero': 'read',
            'report-observation': 'read', 'thread': 'read',
            'memset': 'write', 'memcpy-src': 'read', 'memcpy-dst': 'write',
            'memmove': 'write', 'strdup': 'read', 'strndup': 'read', 'posix': 'write'}
        for flags, name in variants:
            run([cxx, *common, *flags, '-fno-builtin', '-pthread',
                 root/'TestCases/temporal/lifecycle.cpp', '-o', exe])
            for mode in ['', 'zero-length', 'realloc-failure', 'fallback',
                         'next-generation', 'wrap-reuse']:
                run([exe, *([mode] if mode else [])]); checks += 1
            for mode, operation in failures.items():
                reason = {'zero-tag': 'zero managed tag', 'never': 'never allocated',
                          'never-zero': 'never allocated',
                          'report-observation': 'never allocated'}.get(
                    mode, 'generation mismatch')
                run([exe, mode], ['operation = ' + operation, 'reason = ' + reason]); checks += 1
            if custom:
                for mode in ['geometry', 'geometry-tail', 'geometry-zero', 'geometry-tail-zero',
                             'geometry-legacy', 'geometry-tail-legacy']:
                    run([exe, mode], 'unavailable (invalid slot geometry)'); checks += 1
            print(build.name, name, 'passed', flush=True)

        for flags, name in variants:
            run([cxx, *common, *flags, '-fno-vectorize', '-fno-slp-vectorize',
                 '-fno-unroll-loops', root/'TestCases/temporal/loops.cpp', '-o', exe])
            run([exe]); checks += 1
            for mode in ['stale', 'overflow', 'write', 'call', 'reuse', 'fixed']:
                run([exe, mode], 'generation mismatch'); checks += 1

            if custom:
                for mode in ['geometry', 'geometry-tail']:
                    run([exe, mode], 'unavailable (invalid slot geometry)'); checks += 1
        run([cxx, *common, '-O2', '-mllvm', '-flexfat-check-whole-access=true',
             '-fno-vectorize', '-fno-slp-vectorize', '-fno-unroll-loops',
             root/'TestCases/temporal/loops.cpp', '-o', exe])
        run([exe, 'width'], 'out-of-bounds'); checks += 1

        if args.storage == 'shadow':
            # Arithmetic validates the bias table and the POW2 fixed shadow.
            arithmetic_obj = d/'arithmetic.o'
            config_flags = ['-DFLEXFAT_TEMPORAL_TBI', '-I'+str(root.parent.parent/'lib')]
            if custom:
                config_flags += ['-DFLEXFAT_CUSTOM_CONFIG',
                                 '-I'+str(build/'lib/Transforms/Instrumentation')]
            run([cxx, '-O2', *config_flags, '-c', root/'TestCases/temporal/tbi-slot-arithmetic.cpp',
                 '-o', arithmetic_obj])
            run([cxx, *common, arithmetic_obj, '-o', exe]); run([exe]); checks += 1

        # Old TBI objects must be rebuilt: both old marker versions fail.
        old_obj = d/'v1.o'
        run([cc, '-O2', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
             '-c', root/'TestCases/temporal/tbi-v1-object.c', '-o', old_obj])
        run([cc, *common, old_obj, '-Wl,--gc-sections', '-o', exe],
            '__flexfat_tbi_abi_v1'); checks += 1
        v2 = d/'v2.c'
        v2.write_text('extern void __flexfat_tbi_abi_v2(void);\n'
                      '__attribute__((constructor)) static void init(void) {'
                      '__flexfat_tbi_abi_v2();}\nint main(void) {return 0;}\n')
        run([cc, '-c', v2, '-o', old_obj])
        run([cc, *common, old_obj, '-Wl,--gc-sections', '-o', exe],
            '__flexfat_tbi_abi_v2'); checks += 1

        # Empty object retains the ABI contract even with section GC.
        empty = d/'empty.c'; empty.write_text('int main(void) { return 0; }\n')
        obj = d/'empty.o'
        run([cc, *common, '-O2', '-ffunction-sections', '-fdata-sections', '-c', empty, '-o', obj])
        run([cc, *common, obj, '-Wl,--gc-sections', '-o', exe]); run([exe])
        run([cc, '-fsanitize=flexfat', obj, '-Wl,--gc-sections', '-o', d/'wrong'], abi)
        run([cc, obj, '-Wl,--gc-sections', '-o', d/'missing'], abi)
        # Older runtime ABIs cannot satisfy a current instrumented object.
        old_runtime = d/'old-runtime.c'
        old_runtime.write_text('void __flexfat_tbi_abi_v1(void) {}\n'
                               'void __flexfat_tbi_abi_v2(void) {}\n' +
                               ('' if custom else 'void __flexfat_tbi_abi_v3(void) {}\n') +
                               ('void __flexfat_tbi_abi_prior_byte_' +
                                ('custom' if custom else 'pow2') + '_v1(void) {}\n'
                                if args.storage == 'prior-byte' else ''))
        run([cc, obj, old_runtime, '-Wl,--gc-sections', '-o', d/'old-runtime'],
            abi)
        checks += 4
        launcher = d/'launcher'
        run([cc, root/'TestCases/temporal/tbi-init-failure.c', '-o', launcher])
        run([launcher, exe], 'initialization failed: PR_SET_TAGGED_ADDR_CTRL, errno=1'); checks += 1

        # An uninstrumented preinit hook precedes runtime initialization and
        # forces allocations/memory operations through initialization guards.
        startup = d/'startup.o'
        run([cc, '-fno-builtin', '-c', root/'TestCases/temporal/tbi-startup.c', '-o', startup])
        runtime_name = ('flexfat_tbi_' + args.storage.replace('-', '_')
                        if args.storage != 'shadow' else 'flexfat_tbi')
        runtime = run([cc, '-print-file-name=libclang_rt.' + runtime_name + '.a']).strip()
        # The driver normally puts its runtime first. Explicit ordering places
        # this test hook ahead of the runtime's preinit entry.
        run([cc, startup, '-Wl,--whole-archive', runtime,
             '-Wl,--no-whole-archive', '-lpthread', '-ldl', '-lrt', '-lm', '-o', exe])
        run([exe]); checks += 1
        run([cc, root/'TestCases/temporal/tbi-reentrant.c', '-fno-builtin', '-c', '-o', startup])
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

        if args.storage == 'last-byte':
            case = root/'TestCases/temporal/last_byte.cpp'
            config_flags = ['-I'+str(root.parent.parent/'lib')]
            if custom:
                config_flags += ['-DFLEXFAT_CUSTOM_CONFIG',
                                 '-I'+str(build/'lib/Transforms/Instrumentation')]
            for flags, name in [(['-O0'], 'O0'), (['-O2'], 'O2'),
                                (['-O2', '-mllvm', '-flexfat-alignment=right'], 'right')]:
                run([cxx, *common, *flags, *config_flags, '-fno-builtin', case, '-o', exe])
                run([exe]); run([exe, 'onepast']); run([exe, 'maps'])
                run([exe, 'wide']); checks += 4
                for mode in ['byte', 'memset', 'adjacent-tag']:
                    run([exe, mode], 'out-of-bounds'); checks += 1
                print(build.name, 'last-byte', name, 'passed', flush=True)
            run([cxx, *common, '-O2', '-fsanitize-recover=flexfat',
                 *config_flags, '-fno-builtin', case, '-o', exe])
            run([exe, 'wide']); checks += 1
            for mode in ['byte', 'memset', 'adjacent-tag']:
                run([exe, mode], 'out-of-bounds'); checks += 1
            p = subprocess.run([str(exe), 'recover-other'], text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               timeout=90)
            assert p.returncode == 0 and 'FLEXFAT WARNING' in p.stderr, (p.returncode, p.stderr)
            checks += 1

        if args.storage == 'prior-byte':
            case = root/'TestCases/temporal/prior_byte.cpp'
            config_flags = ['-I'+str(root.parent.parent/'lib')]
            if custom:
                config_flags += ['-DFLEXFAT_CUSTOM_CONFIG',
                                 '-I'+str(build/'lib/Transforms/Instrumentation')]
            for flags, name in [(['-O0'], 'O0'), (['-O2'], 'O2')]:
                run([cxx, *common, *flags, *config_flags, '-fno-builtin',
                     case, '-o', exe])
                run([exe]); checks += 1
                for mode in ['tag-byte', 'adjacent-tag', 'memset']:
                    run([exe, mode], 'out-of-bounds'); checks += 1
                print(build.name, 'prior-byte', name, 'passed', flush=True)
        if args.storage != 'shadow':
            mixed = d/'mixed.o'
            run([cc, '-fsanitize=flexfat', '-fsanitize-flexfat-tbi', '-c',
                 empty, '-o', mixed])
            run([cc, *common, mixed, '-o', d/'mixed'],
                '__flexfat_tbi_abi_v3' if custom else '__flexfat_tbi_abi_v4')
            checks += 1

        results.append({'build': str(build), 'storage': args.storage, 'checks': checks,
                        'metadata_entry_bytes': metadata_bytes,
                        'metadata_reservation_bytes': reservation_bytes,
                        'timings': {}})
        print(json.dumps(results[-1], indent=2), flush=True)
if args.output:
    args.output.write_text(json.dumps(results, indent=2)+'\n')
