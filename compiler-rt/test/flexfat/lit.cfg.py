# -*- Python -*-

import os

# Setup config name.
config.name = "FlexFat" + config.name_suffix

# Setup source root.
config.test_source_root = os.path.dirname(__file__)

# Default test suffixes.
config.suffixes = [".c", ".cpp"]

# FlexFat is x86_64-only.
if config.target_arch != "x86_64":
    config.unsupported = True


def build_invocation(compile_flags):
    return " " + " ".join([config.clang] + compile_flags) + " "


config.substitutions.append(("%clang ", build_invocation([config.target_cflags])))
config.substitutions.append(
    ("%clangxx ", build_invocation(config.cxx_mode_flags + [config.target_cflags]))
)

# Unit 1: -fsanitize=flexfat is not wired into the clang driver yet (Unit 6).
# %clang_flexfat names the eventual end-to-end form now so TestCases can be
# written; the sentinel that uses it is marked XFAIL until the driver flag lands.
config.substitutions.append(
    ("%clang_flexfat ", build_invocation([config.target_cflags, "-fsanitize=flexfat"]))
)

# Until the -fsanitize=flexfat driver flag exists (Unit 6), e2e tests link the
# runtime by compiling its source directly (like the config tests compile the
# generator). Used by the OOB-reporter error-text test.
_flexfat_src = getattr(config, "flexfat_src_dir", "")
config.substitutions.append(
    (
        "%clang_flexfat_runtime ",
        build_invocation(
            [
                config.target_cflags,
                "-I" + _flexfat_src,
                os.path.join(_flexfat_src, "lowfat.c"),
                "-lpthread",
                "-ldl",
            ]
        ),
    )
)
