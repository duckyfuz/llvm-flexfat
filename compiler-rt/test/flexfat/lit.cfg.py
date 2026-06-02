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

# The end-to-end form: -fsanitize=flexfat (wired into the clang driver in
# Unit 6). This links libclang_rt.flexfat and runs the FlexFat pass.
config.substitutions.append(
    ("%clang_flexfat ", build_invocation([config.target_cflags, "-fsanitize=flexfat"]))
)

# Same driver path, plus the runtime source dir on the include path so e2e tests
# that call the runtime ABI directly (e.g. lowfat_oob_error) can see <lowfat.h>.
# Unit 6 re-points this from the old direct-lowfat.c-compile workaround to the
# real -fsanitize=flexfat flag, so the error-text test now exercises the flag.
_flexfat_src = getattr(config, "flexfat_src_dir", "")
config.substitutions.append(
    (
        "%clang_flexfat_runtime ",
        build_invocation(
            [config.target_cflags, "-fsanitize=flexfat", "-I" + _flexfat_src]
        ),
    )
)
