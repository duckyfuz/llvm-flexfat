#!/usr/bin/env bash

set -euo pipefail

# Toggle configuration based on argument.  FLEXFAT_BUILD_DIR lets callers keep
# POW2 and custom builds separate; FLEXFAT_SIZES_CFG overrides the default
# custom size table.
build_dir=${FLEXFAT_BUILD_DIR:-build}
sizes_cfg=${FLEXFAT_SIZES_CFG:-"$PWD/compiler-rt/lib/flexfat/tools/sizes.cfg"}
layout=${1:-}

if [[ "$layout" == "custom" ]]; then
    echo "[+] Configuring custom FlexFat sizes..."
    cmake -DFLEXFAT_SIZES_CFG="$sizes_cfg" "$build_dir"
elif [[ "$layout" == "pow2" ]]; then
    echo "[+] Configuring default pow2 FlexFat sizes..."
    cmake -UFLEXFAT_SIZES_CFG "$build_dir"
else
    echo "Usage: $0 [custom|pow2]"
    exit 1
fi

# Symlink compile_commands.json for LSP
echo "[+] Updating compile_commands.json symlink..."
ln -sf "$build_dir/compile_commands.json" .

# Build and run tests
echo "[+] Building..."
ninja -C "$build_dir"

echo "[+] Running FlexFat tests..."
ninja -C "$build_dir/runtimes/runtimes-bins" compiler-rt/test/flexfat/check-flexfat
