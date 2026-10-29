#!/usr/bin/env python3
"""Native acceptance harness. Pass one or more matching build dirs."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__, allow_abbrev=False)
parser.add_argument('builds', nargs='+', type=Path)
parser.add_argument('--output', type=Path)
parser.add_argument('--tbi', choices=['shadow', 'packed-shadow', 'last-byte', 'prior-byte'],
                    default='last-byte')
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
    common = ['-fsanitize=flexfat', '-fsanitize-flexfat-tbi=' + args.tbi]
    custom = any(line.startswith('FLEXFAT_SIZES_CFG:') and line.split('=',1)[1]
                 for line in (build/'CMakeCache.txt').read_text().splitlines())
    if custom and args.tbi in ('shadow', 'packed-shadow'):
        raise SystemExit(args.tbi + ' TBI storage requires a POW2 build')
    profile = 'custom' if custom else 'pow2'
    abis = {
        'shadow': '__flexfat_tbi_abi_v7',
        'packed-shadow': '__flexfat_tbi_abi_packed_shadow_pow2_v1',
        'last-byte': f"__flexfat_tbi_abi_last_byte_{profile}_v2",
        'prior-byte': f"__flexfat_tbi_abi_prior_byte_{profile}_v3",
    }
    abi = abis[args.tbi]
    config_flags = ['-I' + str(root.parent.parent / 'lib')]
    if custom:
        config_flags += ['-DFLEXFAT_CUSTOM_CONFIG',
                         '-I' + str(build / 'lib/Transforms/Instrumentation')]
    metadata_bytes = (sum((1 << 32) // (1 << n) for n in range(4, 31))
                      if args.tbi in ('shadow', 'packed-shadow') else 0)
    reservation_bytes = {
        'shadow': 1 << 44,
        'packed-shadow': (1 << 29) + os.sysconf('SC_PAGE_SIZE'),
    }.get(args.tbi, 0)
    checks = 0
    with tempfile.TemporaryDirectory(prefix='flexfat-tbi-') as directory:
        d = Path(directory)
        exe = d/'test'
        # Spatial tables have identical allocation rules in every TBI mode;
        # temporal storage must not fill their unused ABI slots or gaps.
        metadata_obj = d/'metadata.o'
        run([cxx, '-O2', *config_flags, '-c',
             root/'TestCases/core/metadata_mappings.cpp', '-o', metadata_obj])
        run([cxx, *common, metadata_obj, '-o', exe])
        run([exe])
        checks += 1
        variants = [(['-O'+level], 'O'+level)
                    for level in ['0', '1', '2', '3', 's', 'z']]
        failures = {
            'read': 'read', 'write': 'write', 'reuse': 'read', 'wrap-free': 'read',
            'double-free': 'free', 'stale-free': 'free', 'realloc': 'realloc',
            'stale-realloc': 'realloc', 'realloc-zero': 'realloc',
            'zero-tag': 'read', 'never': 'read', 'tagged-foreign-runtime': 'read',
            'report-observation': 'read', 'thread': 'read',
            'memset': 'write', 'memcpy-src': 'read', 'memcpy-dst': 'write',
            'memmove': 'write', 'strdup': 'read', 'strndup': 'read', 'posix': 'write'}
        for flags, name in variants:
            run([cxx, *common, *flags, '-fno-builtin', '-pthread',
                 root/'TestCases/temporal/lifecycle.cpp', '-o', exe])
            for mode in ['', 'zero-length', 'realloc-failure', 'fallback',
                         'next-generation', 'wrap-reuse', 'fresh-zero', 'never-zero']:
                run([exe, *([mode] if mode else [])])
                checks += 1
            for mode, operation in failures.items():
                run([exe, mode], ['operation = ' + operation,
                                  'reason = generation mismatch'])
                checks += 1
            if custom:
                for mode in ['geometry', 'geometry-tail', 'geometry-zero', 'geometry-tail-zero',
                             'geometry-legacy', 'geometry-tail-legacy',
                             'geometry-legacy-zero', 'geometry-tail-legacy-zero']:
                    run([exe, mode], 'unavailable (invalid slot geometry)')
                    checks += 1
            print(build.name, name, 'passed', flush=True)

        for flags, name in variants:
            run([cxx, *common, *flags, '-fno-vectorize', '-fno-slp-vectorize',
                 '-fno-unroll-loops', root/'TestCases/temporal/loops.cpp', '-o', exe])
            run([exe])
            checks += 1
            run([exe, 'tagged-foreign'], 'generation mismatch')
            checks += 1
            for mode in ['stale', 'overflow', 'write', 'call', 'reuse', 'fixed']:
                run([exe, mode], 'generation mismatch')
                checks += 1

            if custom:
                for mode in ['geometry', 'geometry-tail']:
                    run([exe, mode], 'unavailable (invalid slot geometry)')
                    checks += 1
        run([cxx, *common, '-O2', '-fsanitize-flexfat-check-whole-access',
             '-fno-vectorize', '-fno-slp-vectorize', '-fno-unroll-loops',
             root/'TestCases/temporal/loops.cpp', '-o', exe])
        run([exe, 'width'], 'out-of-bounds')
        checks += 1

        if args.tbi in ('shadow', 'packed-shadow'):
            source = ('tbi-slot-arithmetic.cpp' if args.tbi == 'shadow'
                      else 'packed-shadow.cpp')
            arithmetic_obj = d / 'arithmetic.o'
            run([cxx, '-O2', '-DFLEXFAT_TEMPORAL_TBI', *config_flags,
                 '-c', root / 'TestCases/temporal' / source, '-o', arithmetic_obj])
            run([cxx, *common, arithmetic_obj, '-o', exe])
            run([exe])
            checks += 1

        old_obj = d/'v1.o'
        run([cc, '-O2', '-fno-builtin', '-ffunction-sections', '-fdata-sections',
             '-c', root/'TestCases/temporal/tbi-v1-object.c', '-o', old_obj])
        run([cc, *common, old_obj, '-Wl,--gc-sections', '-o', exe],
            '__flexfat_tbi_abi_v1')
        checks += 1
        obsolete_abis = ['__flexfat_tbi_abi_v2']
        if custom:
            obsolete_abis += {
                'last-byte': ['__flexfat_tbi_abi_last_byte_custom_v1'],
                'prior-byte': ['__flexfat_tbi_abi_prior_byte_custom_v1',
                               '__flexfat_tbi_abi_prior_byte_custom_v2'],
            }[args.tbi]
        else:
            obsolete_abis += {
                'last-byte': ['__flexfat_tbi_abi_last_byte_pow2_v1'],
                'prior-byte': ['__flexfat_tbi_abi_prior_byte_pow2_v2'],
                'packed-shadow': ['__flexfat_tbi_abi_v7'],
                'shadow': ['__flexfat_tbi_abi_v3', '__flexfat_tbi_abi_v4',
                           '__flexfat_tbi_abi_v6'],
            }[args.tbi]
        for old_abi in obsolete_abis:
            old_source = d / 'old-abi.c'
            old_source.write_text(f'extern void {old_abi}(void);\n'
                                  '__attribute__((constructor)) static void init(void) {'
                                  f'{old_abi}();}}\nint main(void) {{return 0;}}\n')
            run([cc, '-c', old_source, '-o', old_obj])
            run([cc, *common, old_obj, '-Wl,--gc-sections', '-o', exe], old_abi)
            checks += 1

        # Empty object retains the ABI contract even with section GC.
        empty = d/'empty.c'
        empty.write_text('int main(void) { return 0; }\n')
        obj = d/'empty.o'
        run([cc, *common, '-O2', '-ffunction-sections', '-fdata-sections', '-c', empty, '-o', obj])
        run([cc, *common, obj, '-Wl,--gc-sections', '-o', exe])
        run([exe])
        run([cc, '-fsanitize=flexfat', obj, '-Wl,--gc-sections', '-o', d/'wrong'], abi)
        run([cc, obj, '-Wl,--gc-sections', '-o', d/'missing'], abi)
        old_runtime = d/'old-runtime.c'
        old_runtime.write_text(''.join(
            f'void {symbol}(void) {{}}\n'
            for symbol in ['__flexfat_tbi_abi_v1', *obsolete_abis]))
        run([cc, obj, old_runtime, '-Wl,--gc-sections', '-o', d/'old-runtime'],
            abi)
        checks += 4
        launcher = d/'launcher'
        run([cc, root/'TestCases/temporal/tbi-init-failure.c', '-o', launcher])
        run([launcher, exe], 'initialization failed: PR_SET_TAGGED_ADDR_CTRL, errno=1')
        checks += 1

        startup = d/'startup.o'
        run([cc, '-fno-builtin', '-c', root/'TestCases/temporal/tbi-startup.c', '-o', startup])
        runtime_name = ('flexfat_tbi_' + args.tbi.replace('-', '_')
                        if args.tbi != 'shadow' else 'flexfat_tbi')
        runtime = run([cc, '-print-file-name=libclang_rt.' + runtime_name + '.a']).strip()
        # The driver normally puts its runtime first. Explicit ordering places
        # this test hook ahead of the runtime's preinit entry.
        run([cc, startup, '-Wl,--whole-archive', runtime,
             '-Wl,--no-whole-archive', '-lpthread', '-ldl', '-lrt', '-lm', '-o', exe])
        run([exe])
        checks += 1
        if args.tbi in ('shadow', 'packed-shadow'):
            occupied = d/'occupied-shadow.c'
            occupied.write_text('''#include <sys/mman.h>
#include <unistd.h>
static void occupy(void) {
  void *address = (void *)0x200000000000ULL;
  if (mmap(address, 4096, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) != address)
    _exit(2);
}
__attribute__((section(".preinit_array"), used))
static void (*hook)(void) = occupy;
int main(void) { return 0; }
''')
            run([cc, '-c', occupied, '-o', startup])
            run([cc, startup, '-Wl,--whole-archive', runtime,
                 '-Wl,--no-whole-archive', '-lpthread', '-ldl', '-lrt', '-lm', '-o', exe])
            run([exe], 'initialization failed: fixed temporal shadow')
            checks += 1
        run([cc, root/'TestCases/temporal/tbi-reentrant.c', '-fno-builtin', '-c', '-o', startup])
        run([cc, startup, *common, '-Wl,--wrap=dlsym', '-o', exe])
        run([exe])
        checks += 1

        lib = d/'lib.c'
        lib.write_text('int dso_load(int *p) { return *p; }\n')
        main = d/'main.c'
        main.write_text('''#include <stdlib.h>
extern int dso_load(int *);
int main(int argc, char **argv) { int *p=malloc(sizeof(int)); *p=7;
if(argc>1) free(p); return dso_load(p)==7 ? 0 : 1; }
''')
        run([cc, *common, '-O2', '-fPIC', '-shared', lib, '-o', d/'libtest.so'])
        run([cc, *common, '-O2', main, '-L'+str(d), '-ltest', '-Wl,-rpath,'+str(d), '-o', exe])
        run([exe])
        run([exe, 'stale'], 'operation = read')
        checks += 2

        layout_cases = {
            'last-byte': ('last_byte.cpp', ['', 'onepast', 'maps', 'wide'],
                          ['byte', 'memset', 'adjacent-tag']),
            'prior-byte': ('prior_byte.cpp', [''], ['tag-byte', 'adjacent-tag', 'memset']),
        }
        if args.tbi in layout_cases:
            source, successes, failures = layout_cases[args.tbi]
            for flags, name in [(['-O0'], 'O0'), (['-O2'], 'O2')]:
                run([cxx, *common, *flags, *config_flags, '-fno-builtin',
                     root / 'TestCases/temporal' / source, '-o', exe])
                for mode in successes:
                    run([exe, *([mode] if mode else [])])
                    checks += 1
                for mode in failures:
                    run([exe, mode], 'out-of-bounds')
                    checks += 1
                print(build.name, args.tbi, name, 'passed', flush=True)
        for other in ['shadow', 'packed-shadow', 'last-byte', 'prior-byte']:
            if other == args.tbi or (custom and other in ('shadow', 'packed-shadow')):
                continue
            mixed = d/'mixed.o'
            run([cc, '-fsanitize=flexfat', '-fsanitize-flexfat-tbi=' + other, '-c', empty, '-o', mixed])
            run([cc, *common, mixed, '-Wl,--gc-sections', '-o', d/'mixed'], abis[other])
            checks += 1

        results.append({'build': str(build), 'storage': args.tbi, 'checks': checks,
                        'metadata_entry_bytes': metadata_bytes,
                        'metadata_reservation_bytes': reservation_bytes})
        print(json.dumps(results[-1], indent=2), flush=True)
if args.output:
    args.output.write_text(json.dumps(results, indent=2)+'\n')
