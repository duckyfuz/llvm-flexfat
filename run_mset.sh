#!/usr/bin/env bash
# Build and test both FlexFat layouts, then evaluate each with MSET.
#
# Usage:
#   ./run_mset.sh /path/to/MSET flexfat/main [MSET options]
#
# The script deliberately delegates the LLVM build and regression-test steps
# to the helper scripts from this checkout.  They operate on the current
# directory, so the runner executes them while positioned in the revision
# worktree.  The requested revision must contain the custom-layout CMake hooks
# and the check-flexfat target used by those helpers.  MSET runs only after
# those checks pass for both layouts.

set -euo pipefail

usage() {
  printf '%s\n' \
    "Usage: $0 MSET_PATH REVISION [MSET options]" \
    '' \
    'Builds and tests POW2 and custom-size FlexFat layouts, then runs MSET for each.' \
    'The custom layout uses compiler-rt/lib/flexfat/tools/sizes.cfg by default;' \
    'override it with FLEXFAT_MSET_SIZES_CFG=/path/to/sizes.cfg.' \
    'The revision must support FLEXFAT_SIZES_CFG and the check-flexfat target.' \
    '' \
    'Optional MSET compile controls:' \
    '  FLEXFAT_MSET_OPT_LEVEL=2              (default: 2)' \
    '  FLEXFAT_MSET_FLEXFAT_MODE=fast         (fast, safe, or right-align)' \
    '  FLEXFAT_MSET_FLEXFAT_PLACEMENT=scalar-late' \
    '  FLEXFAT_MSET_EXTRA_SANITIZER_FLAGS="..."' \
    '' \
    'Results are written to mset-results/ (or $MSET_OUTPUT_DIR).' >&2
  exit 2
}

[[ $# -ge 2 ]] || usage

mset_path=$1
revision=$2
shift 2

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
mset_path=$(cd "$mset_path" && pwd)
[[ -f "$mset_path/src/CMakeLists.txt" ]] || {
  echo "error: '$mset_path' does not look like an MSET checkout" >&2
  exit 2
}

if git -C "$repo_root" rev-parse --verify --quiet "${revision}^{commit}" >/dev/null; then
  resolved_revision=$revision
elif git -C "$repo_root" rev-parse --verify --quiet "origin/${revision}^{commit}" >/dev/null; then
  resolved_revision="origin/$revision"
else
  echo "error: cannot resolve LLVM revision '$revision'" >&2
  exit 2
fi

revision_sha=$(git -C "$repo_root" rev-parse "$resolved_revision")
revision_tag=${revision_sha:0:12}
revision_name=${revision//[^A-Za-z0-9._-]/_}
timestamp=$(date +%Y%m%d-%H%M%S)

cache_root=${FLEXFAT_MSET_CACHE_DIR:-"${TMPDIR:-/tmp}/flexfat-mset-builds"}
worktree="$cache_root/${revision_name}-${revision_tag}"
output_dir=${MSET_OUTPUT_DIR:-"$repo_root/mset-results"}
custom_sizes=${FLEXFAT_MSET_SIZES_CFG:-"$repo_root/compiler-rt/lib/flexfat/tools/sizes.cfg"}
opt_level=${FLEXFAT_MSET_OPT_LEVEL:-2}
flexfat_mode=${FLEXFAT_MSET_FLEXFAT_MODE:-fast}
flexfat_placement=${FLEXFAT_MSET_FLEXFAT_PLACEMENT:-scalar-late}
extra_sanitizer_flags=${FLEXFAT_MSET_EXTRA_SANITIZER_FLAGS:-}

[[ -f "$custom_sizes" ]] || {
  echo "error: custom size configuration does not exist: $custom_sizes" >&2
  exit 2
}
[[ "$opt_level" =~ ^[0-3sSzZ]$ ]] || {
  echo "error: FLEXFAT_MSET_OPT_LEVEL must be one of 0, 1, 2, 3, s, or z" >&2
  exit 2
}
case "$flexfat_mode" in fast|safe|right-align) ;; *) echo "error: unsupported FlexFat mode: $flexfat_mode" >&2; exit 2;; esac
case "$flexfat_placement" in optimizer-early|scalar-late|optimizer-last) ;; *) echo "error: unsupported FlexFat placement: $flexfat_placement" >&2; exit 2;; esac

mkdir -p "$cache_root" "$output_dir"

if [[ ! -e "$worktree/.git" ]]; then
  echo "[+] Creating cached worktree for $resolved_revision"
  git -C "$repo_root" worktree add --detach "$worktree" "$resolved_revision"
else
  cached_sha=$(git -C "$worktree" rev-parse HEAD)
  [[ "$cached_sha" == "$revision_sha" ]] || {
    echo "error: cached worktree '$worktree' is at $cached_sha, expected $revision_sha" >&2
    exit 1
  }
  echo "[+] Reusing cached worktree for $resolved_revision"
fi

# The helpers are intentionally taken from the current checkout so they can
# use isolated build directories.  Reject historical revisions that do not
# have the source-side CMake hooks those helpers require, rather than risking
# a build that silently falls back to an incompatible configuration.
pass_config="$worktree/llvm/lib/Transforms/Instrumentation/FlexFatPassConfig.cmake"
runtime_config="$worktree/compiler-rt/lib/flexfat/CMakeLists.txt"
test_config="$worktree/compiler-rt/test/flexfat/CMakeLists.txt"
if [[ ! -f "$pass_config" || ! -f "$runtime_config" || ! -f "$test_config" ]] || \
   ! rg -q 'FLEXFAT_SIZES_CFG' "$pass_config" "$runtime_config" || \
   ! rg -q 'add_lit_testsuite\(check-flexfat' "$test_config"; then
  echo "error: revision '$resolved_revision' is incompatible with this runner" >&2
  echo "       it must provide FLEXFAT_SIZES_CFG support and the check-flexfat target" >&2
  exit 2
fi

# The custom config is copied into the worktree so historical/compiler builds
# receive an immutable path and the build cache can be identified by its hash.
custom_sizes_hash=$(cksum "$custom_sizes" | awk '{print $1}')
custom_sizes_copy="$worktree/mset-custom-sizes-${custom_sizes_hash}.cfg"
if [[ ! -f "$custom_sizes_copy" ]] || ! cmp -s "$custom_sizes" "$custom_sizes_copy"; then
  cp "$custom_sizes" "$custom_sizes_copy"
fi

mset_build_dir="$mset_path/build"
echo "[+] Building MSET"
cmake -S "$mset_path/src" -B "$mset_build_dir"
cmake --build "$mset_build_dir" --parallel "${FLEXFAT_MSET_JOBS:-6}"

make_mset_config() {
  local layout=$1
  local clang=$2
  local config_file=$3
  local sanitizer_flags="-fsanitize=flexfat -mllvm -flexfat-placement=${flexfat_placement}"
  if [[ "$flexfat_mode" != fast ]]; then
    sanitizer_flags+=" -mllvm -flexfat-mode=${flexfat_mode}"
  fi
  if [[ -n "$extra_sanitizer_flags" ]]; then
    sanitizer_flags+=" ${extra_sanitizer_flags}"
  fi

  # FlexFat defaults to fatal exit code 1.  MSET classifies exit code 6 as a
  # detection, so set that runtime option explicitly and record it in XML.
  printf '%s\n' \
    '<sanitizer>' \
    "  <name>FlexFat (${layout}; ${flexfat_mode}; ${flexfat_placement}; -O${opt_level})</name>" \
    '  <setup_baseline>' \
    "    <compile_cmd><![CDATA[${clang} -O${opt_level} -g -Wl,-T,${mset_path}/sanitizers/after_text.ld \$SOURCE_FILE -o \$GENERATED_BINARY]]></compile_cmd>" \
    '  </setup_baseline>' \
    '  <run_baseline>$GENERATED_BINARY</run_baseline>' \
    '  <setup>' \
    "    <compile_cmd><![CDATA[${clang} -O${opt_level} -g -Wl,-T,${mset_path}/sanitizers/after_text.ld ${sanitizer_flags} \$SOURCE_FILE -o \$GENERATED_BINARY]]></compile_cmd>" \
    '  </setup>' \
    '  <run_env_args>' \
    '    <env_var name="FLEXFAT_OPTIONS">exitcode=6</env_var>' \
    '  </run_env_args>' \
    '  <run>$GENERATED_BINARY</run>' \
    '  <bug_detected_exit_values><value>6</value></bug_detected_exit_values>' \
    '</sanitizer>' >"$config_file"
}

run_layout() {
  local layout=$1
  local sizes_cfg=$2
  local build_dir="$worktree/build-mset-${layout}"
  local clang="$build_dir/bin/clang"
  local config_file
  local output_file="$output_dir/${revision_name}-${revision_tag}-${layout}-${flexfat_mode}-${flexfat_placement}-O${opt_level}-${timestamp}.log"
  config_file=$(mktemp "${TMPDIR:-/tmp}/flexfat-mset-${layout}.XXXXXX.xml")

  echo "[+] Configuring ${layout} build through configure_llvm.sh"
  (
    cd "$worktree"
    FLEXFAT_BUILD_DIR="$build_dir" \
      FLEXFAT_SIZES_CFG="$sizes_cfg" \
      FLEXFAT_JOBS="${FLEXFAT_MSET_JOBS:-6}" \
      "$repo_root/configure_llvm.sh"
  )

  echo "[+] Building and testing ${layout} through run_flexfat.sh"
  (
    cd "$worktree"
    FLEXFAT_BUILD_DIR="$build_dir" \
      FLEXFAT_SIZES_CFG="$sizes_cfg" \
      "$repo_root/run_flexfat.sh" "$layout"
  )

  [[ -x "$clang" ]] || { echo "error: clang was not built at $clang" >&2; exit 1; }
  make_mset_config "$layout" "$clang" "$config_file"

  echo "[+] Running MSET for ${layout}; output: $output_file"
  (
    cd "$mset_build_dir"
    ./mset --evaluate "$config_file" "$@"
  ) 2>&1 | tee "$output_file"
  rm -f "$config_file"
}

run_layout pow2 ""
run_layout custom "$custom_sizes_copy"
