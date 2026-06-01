# -*- Python -*-
#
# Self-contained lit config for the FlexFat config/table golden-parity tests.
#
# These diff generated config artifacts (lowfat_config.{c,h}, lowfat.ld) against
# committed golden files. Unit 1 ships only a trivial green sentinel; the config
# generator (a port of the reference lowfat-config.c) and its real golden tables
# land in a later unit. This config is intentionally standalone (no lit.site.cfg)
# so the single check-flexfat target can run this directory directly.

import os
import lit.formats

config.name = "FlexFatConfig"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = [".test"]
config.test_source_root = os.path.dirname(__file__)
config.test_exec_root = config.test_source_root
