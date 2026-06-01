# FlexFat — LLVM / toolchain notes

Environment of record for the FlexFat reimplementation. Branch
`flexfat/reimplementation` (based on `llvmorg-22.1.6`).

## In-tree LLVM
- The tree self-reports **clang/LLVM `23.0.0git`** (configure: "Clang version:
  23.0.0git"; runtime resource dir `lib/clang/23/...`). Treat this as
  authoritative for tool versions.
- `git describe` nearest tag = `llvmorg-22-init-37625-g28be1eaf755a`. CLAUDE.md
  describes the fork as the "LLVM 22 fork" based on `llvmorg-22.1.6`; the
  self-reported version (23.0.0git) does not match that label — flagged here so
  later units don't assume a 22.x ABI/header surface. APIs were verified against
  the actual installed headers, not the label.
- New Pass Manager + opaque pointers only. No legacy PassManager,
  no `getPointerElementType`, no `RegisterStandardPasses`.

## Build configuration (existing `build/`)
- `LLVM_ENABLE_PROJECTS = "clang;compiler-rt"` (in-tree projects build).
- `LLVM_ENABLE_RUNTIMES = ""`, `LLVM_TARGETS_TO_BUILD = X86`.
- `CMAKE_BUILD_TYPE = Release`, `LLVM_ENABLE_ASSERTIONS = OFF`.
- `COMPILER_RT_INCLUDE_TESTS = ON`, `COMPILER_RT_ENABLE_WERROR = OFF`.
- As of Unit 1, only `bin/llvm-lit` is built; `opt`/`clang`/`FileCheck` are not
  yet compiled, so the first build of those is the slow step. Subsequent
  rebuilds after touching the pass/runtime are incremental.

## Toolchain versions
| Tool | Version | Notes |
|---|---|---|
| Host build compiler | gcc 13.3.0 (`/usr/bin/cc`, `/usr/bin/c++`) | builds LLVM + compiler-rt |
| System clang | Ubuntu clang 18.1.3 | present on host; NOT used to build the tree |
| In-tree clang / opt / lld | produced by this build (23.0.0git) | used as the test compiler / `opt` |
| llvm-config / opt / ld.lld | not installed system-wide | come from `build/bin` after building |
| CMake | 3.28.3 | |
| Ninja | 1.11.1 | |
| Python | 3.12.3 | lit |
| Host glibc | 2.39 (Ubuntu GLIBC 2.39-0ubuntu8.7) | |
| OS | Linux 6.17, x86_64 | |

## ABI parity (recorded for later units; NOT exercised in Unit 1)
Keep byte-identical to the reference: the `lowfat_*` runtime symbol names; the
fixed tables `_LOWFAT_SIZES` @ `0x200000` and `_LOWFAT_MAGICS` @ `0x300000`;
`LOWFAT_REGION_SIZE_SHIFT = 35` (32 GiB regions); the `LOWFAT_OOB_ERROR_*` info
codes; and the exact `LOWFAT ERROR:` report text. FlexFat is x86_64-only; the
runtime is built with `-mcmodel=large -mbmi -mbmi2 -mlzcnt` to match the
reference. Unit 1 emits none of this — it is a no-op scaffold.
