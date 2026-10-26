import lit.formats
import os
import shlex

config.name = "FlexFatSanitizer" + getattr(config, "name_suffix", "")

config.test_source_root = os.path.dirname(__file__)
config.suffixes = [".c", ".cpp"]

if not hasattr(config, "test_format"):
    config.test_format = lit.formats.ShTest(execute_external=True)


def _find_clang():
    clang = getattr(config, "clang", None)
    if clang:
        return clang
    tools_dir = getattr(config, "llvm_tools_dir", None)
    if tools_dir:
        candidate = os.path.join(tools_dir, "clang++")
        if os.path.isfile(candidate):
            return candidate
    return "clang++"


clang = _find_clang()
config.substitutions.append(("%clang ", " " + clang + " "))


def build_invocation(flags):
    return " " + " ".join([clang] + flags) + " "


flexfat_base = ["-fsanitize=flexfat"]

config.substitutions.append(("%clangxx_flexfat ", build_invocation(flexfat_base)))

if getattr(config, "target_os", "Unknown") not in ["Darwin", "Linux"]:
    if hasattr(config, "target_os"):
        config.unsupported = True

if getattr(config, "flexfat_custom_config", False):
    config.available_features.add("flexfat-custom-config")

config.substitutions.append(
    ("%clangxx_flexfat_tbi ", build_invocation(flexfat_base + ["-fsanitize-flexfat-tbi=last-byte"])))
if (getattr(config, "target_arch", "") == "aarch64" and
        getattr(config, "target_os", "") == "Linux"):
    config.available_features.add("flexfat-tbi")

# Uninstrumented runtime checks need the same geometry as the tested runtime.
if "flexfat-tbi" in config.available_features:
    runtime_build = os.path.join(config.compiler_rt_obj_root, "lib", "flexfat")
    config_flags = [
        "-DFLEXFAT_TEMPORAL_TBI",
        "-I" + os.path.join(config.compiler_rt_src_root, "lib"),
    ]
    if getattr(config, "flexfat_custom_config", False):
        config_flags += ["-DFLEXFAT_CUSTOM_CONFIG", "-I" + runtime_build]
    config.substitutions.append(
        ("%flexfat_config_flags", " ".join(shlex.quote(flag) for flag in config_flags)))
    config.substitutions.append(
        ("%flexfat_tbi_runtime", shlex.quote(os.path.join(
            config.compiler_rt_libdir, "libclang_rt.flexfat_tbi_last_byte.a"))))
