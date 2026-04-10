# -*- Python -*-
"""LIT configuration for LowFat Sanitizer tests."""

import os
import lit.formats

config.name = "LowFat" + config.name_suffix
config.test_format = lit.formats.ShTest(not lit_config.quiet)
config.suffixes = [".c", ".cpp"]
config.test_source_root = os.path.dirname(__file__)

# Clang flags for LowFat-instrumented compilation.
clang = config.target_cc
clang_lowfat_cflags = [
    "-fsanitize=lowfat",
    "-g",
] + config.target_cflags.split()

clang_lowfat_cflags_str = " ".join(clang_lowfat_cflags)

# Substitutions available in test RUN lines.
config.substitutions.append(("%clang_lowfat", clang + " " + clang_lowfat_cflags_str))
config.substitutions.append(
    (
        "%clang_lowfat_recover",
        clang + " " + clang_lowfat_cflags_str + " -fsanitize-recover=lowfat",
    )
)
config.substitutions.append(
    (
        "%clang_lowfat_safe",
        clang + " " + clang_lowfat_cflags_str + " -mllvm -lowfat-mode=safe",
    )
)
config.substitutions.append(
    (
        "%clang_lowfat_rightalign",
        clang + " " + clang_lowfat_cflags_str + " -mllvm -lowfat-mode=right-align",
    )
)

# Feature detection.
if config.host_os == "Darwin":
    config.available_features.add("darwin")
elif config.host_os == "Linux":
    config.available_features.add("linux")

config.available_features.add("lowfat")

# Require the sanitizer runtime to exist.
if not os.path.isdir(config.compiler_rt_libdir):
    lit_config.fatal("compiler-rt library directory not found: " + config.compiler_rt_libdir)
