#!/usr/bin/env bash

set -euo pipefail

# Toggle configuration based on argument.  FLEXFAT_BUILD_DIR lets callers keep
# POW2 and custom builds separate; FLEXFAT_SIZES_CFG overrides the default
# custom size table.
build_dir=${FLEXFAT_BUILD_DIR:-build}
sizes_cfg=${FLEXFAT_SIZES_CFG:-"$PWD/compiler-rt/lib/flexfat/tools/sizes.cfg"}
layout=${1:-}

if [[ "$layout" != "custom" && "$layout" != "pow2" ]]; then
    echo "Usage: $0 [custom|pow2]" >&2
    exit 1
fi

profile_marker="$build_dir/.flexfat-profile"
if [[ -f "$profile_marker" ]]; then
    configured_layout=$(<"$profile_marker")
    if [[ "$configured_layout" != "$layout" ]]; then
        echo "error: $build_dir is frozen for FlexFat profile '$configured_layout', not '$layout'" >&2
        echo "error: use distinct build directories for POW2 and custom profiles" >&2
        exit 2
    fi
fi

if [[ "$layout" == "custom" ]]; then
    echo "[+] Configuring custom FlexFat sizes..."
    cmake -DFLEXFAT_SIZES_CFG="$sizes_cfg" "$build_dir"
elif [[ "$layout" == "pow2" ]]; then
    echo "[+] Configuring default pow2 FlexFat sizes..."
    cmake -UFLEXFAT_SIZES_CFG "$build_dir"
fi
printf '%s\n' "$layout" > "$profile_marker"

# Symlink compile_commands.json for LSP
echo "[+] Updating compile_commands.json symlink..."
ln -sf "$build_dir/compile_commands.json" .

# Build and run tests
echo "[+] Building..."
ninja -C "$build_dir"

verify_cache_profile() {
    local cache=$1
    [[ -f "$cache" ]] || { echo "error: missing profile cache: $cache" >&2; exit 2; }
    local configured
    configured=$(sed -n 's/^FLEXFAT_SIZES_CFG:[^=]*=//p' "$cache")
    if [[ "$layout" == custom && -z "$configured" ]]; then
        echo "error: custom profile lost FLEXFAT_SIZES_CFG in $cache" >&2
        exit 2
    fi
    if [[ "$layout" == pow2 && -n "$configured" ]]; then
        echo "error: POW2 profile retained FLEXFAT_SIZES_CFG=$configured in $cache" >&2
        exit 2
    fi
}

verify_cache_profile "$build_dir/CMakeCache.txt"
verify_cache_profile "$build_dir/runtimes/runtimes-bins/CMakeCache.txt"

probe_dir=$(mktemp -d /tmp/flexfat-profile.XXXXXX)
trap 'rm -rf -- "$probe_dir"' EXIT
printf 'extern void *malloc(unsigned long); extern void sink(void *); void probe(unsigned long n) { char *p = malloc(48); sink(p + n); }\n' > "$probe_dir/probe.c"
"$build_dir/bin/clang" -fsanitize=flexfat -O0 -S -emit-llvm "$probe_dir/probe.c" -o "$probe_dir/probe.ll"
if [[ "$layout" == custom ]]; then
    pass_header="$build_dir/lib/Transforms/Instrumentation/flexfat_config_generated.h"
    runtime_header="$build_dir/runtimes/runtimes-bins/compiler-rt/lib/flexfat/flexfat_config_generated.h"
    cmp -s "$pass_header" "$runtime_header" || {
        echo "error: compiler and runtime generated FlexFat configurations differ" >&2
        exit 2
    }
    grep -q '#define FLEXFAT_REGION_SIZE_LOG     38' "$pass_header"
    grep -q '#define FLEXFAT_TABLES_BASE         UINT64_C(0x300000000000)' "$pass_header"
    grep -q 'lshr i64 .* 38' "$probe_dir/probe.ll" || {
        echo "error: custom compiler does not embed the 38-bit region shift" >&2
        exit 2
    }
else
    grep -q 'lshr i64 .* 32' "$probe_dir/probe.ll" || {
        echo "error: POW2 compiler does not embed the 32-bit region shift" >&2
        exit 2
    }
fi

echo "[+] Running FlexFat tests..."
ninja -C "$build_dir/runtimes/runtimes-bins" compiler-rt/test/flexfat/check-flexfat
