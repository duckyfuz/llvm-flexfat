# -*- Python -*-
#
# Self-contained lit config for the FlexFat allocator-bookkeeping logic test.
#
# This exercises the allocator's page macros, freelist link/unlink and alignment
# math on ORDINARY memory under host ASan+UBSan. The full fixed-address runtime
# cannot boot under ASan (its 2^35 regions collide with ASan's shadow; see
# docs/STATUS.md), so this is where the bookkeeping gets sanitizer coverage. It
# links no fixed-address runtime, so a normal host toolchain (with its asan/ubsan
# runtimes) suffices — no in-tree compiler-rt asan build required.

import os
import shutil
import lit.formats

config.name = "FlexFatLogic"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = [".cpp"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root

_cxx = (shutil.which("clang++") or shutil.which("g++") or shutil.which("c++")
        or "c++")
# .../lib/flexfat/tests/logic -> .../lib/flexfat (for lowfat_malloc_internal.h)
_flexfat_dir = os.path.dirname(os.path.dirname(config.test_source_root))

config.substitutions.append(
    ("%clangxx_asan_ubsan",
     _cxx + " -fsanitize=address,undefined -fno-sanitize-recover=all"
            " -g -O1 -fno-omit-frame-pointer -I" + _flexfat_dir))
config.substitutions.append(("%run", ""))
