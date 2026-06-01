# -*- Python -*-
#
# Self-contained lit config for the FlexFat config/table golden-parity tests.
#
# Each test builds the FlexFat config generator (a standalone host tool, like the
# reference), regenerates the size/magic tables + linker script for one variant,
# and diffs byte-for-byte against the committed golden in ../golden/. This config
# is intentionally standalone (no lit.site.cfg) so the single check-flexfat target
# can run this directory directly.

import os
import shutil
import lit.formats

config.name = "FlexFatConfig"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = [".test"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root

# Host C compiler used to build the generator. %cc resolves cc/gcc/clang on PATH.
_cc = shutil.which("cc") or shutil.which("gcc") or shutil.which("clang") or "cc"
config.substitutions.append(("%cc", _cc))
