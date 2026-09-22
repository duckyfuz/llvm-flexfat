#!/usr/bin/env python3
"""Verify exact-only metadata is absent from basic preprocessing and artifacts."""
import json
from pathlib import Path
import shlex
import subprocess
import sys

for directory in sys.argv[1:]:
    build = Path(directory).resolve()
    runtime_build = build / 'runtimes/runtimes-bins'
    commands = subprocess.check_output(['ninja', '-C', str(runtime_build), '-t', 'commands', 'flexfat'], text=True)
    evidence = {}
    for runtime in ['flexfat_tbi', 'flexfat_tbi_exact']:
        command = next(line for line in commands.splitlines()
                       if f'clang_rt.{runtime}-aarch64.dir/flexfat_rtl.cpp.o' in line and ' -c ' in line)
        words = shlex.split(command)
        flags = [w for w in words if w.startswith(('-D', '-I', '-std='))]
        source = words[words.index('-c') + 1]
        preprocessed = subprocess.check_output([str(build/'bin/clang++'), *flags, '-E', '-P', source], text=True)
        archive = subprocess.check_output([str(build/'bin/clang'), '-print-file-name=libclang_rt.'+runtime+'.a'], text=True).strip()
        strings = subprocess.check_output(['strings', archive], text=True)
        symbols = subprocess.check_output(['nm', '-C', archive], text=True, stderr=subprocess.DEVNULL)
        exact = runtime.endswith('_exact')
        for term in ['user_offsets', 'UserOffset', 'ReportInvalidDeallocation',
                     'flexfat allocation offset metadata initialization', 'not allocation start']:
            assert (term in preprocessed) == exact, (runtime, term, 'preprocessing')
        for term in ['flexfat allocation offset metadata initialization', 'not allocation start']:
            assert (term in strings) == exact, (runtime, term, 'archive')
        assert exact or ('UserOffset(' not in symbols and 'ReportInvalidDeallocation(' not in symbols)
        assert '__flexfat_tbi_abi_v1' in symbols
        if not exact:
            invalid_flags = [f for f in flags if not f.startswith('-DFLEXFAT_TEMPORAL_TBI')]
            invalid = subprocess.run([str(build/'bin/clang++'), *invalid_flags,
                                      '-DFLEXFAT_EXACT_DEALLOCATION=1', '-E', source],
                                     text=True, capture_output=True)
            assert invalid.returncode and 'Exact deallocation requires temporal support' in invalid.stderr
        evidence[runtime] = dict(preprocessing=True, archive=True, temporal_abi=True)
    print(json.dumps(dict(build=str(build), evidence=evidence)))
