#!/usr/bin/env bash

set -euo pipefail

build_jobs=${FLEXFAT_JOBS:-6}
compile_jobs=${FLEXFAT_COMPILE_JOBS:-$build_jobs}
link_jobs=${FLEXFAT_LINK_JOBS:-1}
tablegen_jobs=${FLEXFAT_TABLEGEN_JOBS:-1}
build_dir=${FLEXFAT_BUILD_DIR:-build}
sizes_cfg=${FLEXFAT_SIZES_CFG:-}

# Common flags for all systems
CMAKE_ARGS=(
    -G Ninja -S llvm -B "$build_dir"
    -DCMAKE_BUILD_TYPE=RelWithDebInfo
    -DLLVM_ENABLE_ASSERTIONS=ON
    -DLLVM_ENABLE_PROJECTS="clang;lld"
    -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind"
    -DCLANG_DEFAULT_RTLIB=compiler-rt
    -DCLANG_DEFAULT_LINKER=lld
    -DLLVM_CCACHE_BUILD=ON
    -DLLVM_TARGETS_TO_BUILD=Native
    -DLLVM_OPTIMIZED_TABLEGEN=ON
    -DLLVM_PARALLEL_COMPILE_JOBS="$compile_jobs"
    -DLLVM_PARALLEL_LINK_JOBS="$link_jobs"
    -DLLVM_PARALLEL_TABLEGEN_JOBS="$tablegen_jobs"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ -n "$sizes_cfg" ]]; then
    [[ -f "$sizes_cfg" ]] || {
        echo "error: FLEXFAT_SIZES_CFG does not exist: $sizes_cfg" >&2
        exit 2
    }
    CMAKE_ARGS+=("-DLOWFAT_SIZES_CFG=$sizes_cfg")
else
    # A cached custom build must not silently become the POW2 result.
    CMAKE_ARGS+=(-ULOWFAT_SIZES_CFG)
fi

if [ "$(uname)" == "Darwin" ]; then # macOS-specific
    CMAKE_ARGS+=(
        -DDEFAULT_SYSROOT="$(xcrun --show-sdk-path)"
        -DCLANG_DEFAULT_CXX_STDLIB=libc++
    )
else # Linux & compute cluster (Linux)
    CMAKE_ARGS+=(
        -DCLANG_DEFAULT_UNWINDLIB=libgcc
        -DCLANG_DEFAULT_CXX_STDLIB=libstdc++
        -DBUILD_SHARED_LIBS=ON
        -DLLVM_USE_SPLIT_DWARF=ON
    )
fi

echo "[+] Generating CMake configuration in $build_dir (compile jobs: $compile_jobs; link jobs: $link_jobs; tablegen jobs: $tablegen_jobs)..."
cmake "${CMAKE_ARGS[@]}"
