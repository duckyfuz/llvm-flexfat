# FlexFat — LLVM / toolchain notes

Environment of record for the FlexFat reimplementation. Branch
`flexfat/reimplementation` (based on `llvmorg-22.1.6`).

## In-tree LLVM
- **Confirmed target: LLVM 23-dev (`23.0.0git`).** This is the intended version, not a
  mistake (configure: "Clang version: 23.0.0git"; runtime resource dir `lib/clang/23/...`).
  Authoritative for tool versions and the header/ABI surface we build against.
- HEAD (`git describe` = `llvmorg-22-init-37625-g28be1eaf755a`) descends from our chosen base;
  the tree has since moved onto the 23 development line. Earlier `llvmorg-22.1.6` / "LLVM 22"
  labels are superseded — we track this tree's actual installed headers, which all LLVM
  symbols/signatures are verified against, not a version label.
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

## New-machine environment (recorded 2026-06-06)

Re-established on a fresh host. Different OS/kernel/host-gcc from the original
Ubuntu record above; **glibc unchanged (2.39)** so the TID/JOINID landmine
(`0x2d0`/`0x620`) still validates `OK`. In-tree clang/opt/lld are produced by
this build and report the LLVM-23 versions below.

| Tool | Version | Notes |
|---|---|---|
| OS | Rocky Linux 10.2 (Red Quartz) | kernel `6.12.0-124.56.1.el10_1.x86_64` |
| Host build compiler | gcc 14.3.1 (Red Hat 14.3.1-4) | builds LLVM + compiler-rt |
| In-tree `llvm-config` | 23.0.0git | `build/bin/llvm-config` |
| In-tree `clang` | 23.0.0git, x86_64-unknown-linux-gnu | `build/bin/clang` (Host CPU znver3) |
| In-tree `opt` | 23.0.0git | `build/bin/opt` |
| `ld.lld` | LLD 21.1.8 (system, `/usr/bin/ld.lld`) | not built into `build/bin/`; system-provided |
| Host glibc | 2.39 | `lowfat-check-config` ⇒ `OK` (exit 0) |
| CMake | 3.31.8 | |
| Ninja | 1.11.1 | |
| Python | 3.12.13 | lit |

The `build/` tree persisted across the move (so the "cold rebuild" path is not
actually cold — `check-flexfat` runs against the existing binaries; ninja
rebuilds anything stale incrementally).

**Subsequent finding (2026-06-06):** the persisted `build/` tree's `CMakeCache.txt`
was hard-pinned to the prior machine's source path
(`/home/kenf/Developer/CP4106/llvm-flexfat/llvm`, which does not exist on this
host), so ninja's regen aborted. The tree was wiped and reconfigured fresh
against `/home/kenf/CP4106/llvm-flexfat/llvm` with the original knobs
(`Release`, `clang;compiler-rt`, X86 target, assertions off, tests on). All
binaries above (`clang`, `opt`, `FileCheck`, `libclang_rt.flexfat.a`, …) are
produced by this fresh build; `ninja check-flexfat` ⇒ **46/46 passed** on it.

## REFERENCE pinning (2026-06-06)

The reference LowFat tree at `/home/kenf/Developer/CP4106/llvm-lowfat/` was lost
and has been re-cloned from upstream (`https://github.com/GJDuck/LowFat`).

- **Pinned commit:** `20f8075dd1fd6588700262353c7ba619d82cea8f` (2022-03-27,
  "Fix #23"). From now on, "REFERENCE" means **this specific commit hash**, not
  a mutable directory. If the local tree is ever lost again, re-clone and
  `git checkout 20f8075d` to restore exactly the bytes the SPEC was written
  against.
- **Fingerprint:** the clone's `config/lowfat-config.c` was rebuilt and
  regenerated against `sizes.cfg 32` (non-POW2) and `sizes2.cfg 32` (POW2),
  reusing our committed `flexfat/config/lowfat.errs` for the non-POW2
  precision-error cache (unchanged after the run). Byte-diff vs the committed
  `flexfat/config/golden/`:

  | file | non-POW2 vs golden | POW2 vs golden |
  |---|---|---|
  | `lowfat_config.h` | clean | clean |
  | `lowfat.ld` | clean | clean |
  | `lowfat_config.c` | **1-line diff** | **1-line diff** |
  | `lowfat.errs` | unmodified | (n/a, POW2 has no errs) |

  The single-line divergence in both variants:
  ```
  -#define LOWFAT_JOINID_OFFSET 0x620        # ours (golden)
  +#define LOWFAT_JOINID_OFFSET 0x628        # upstream 20f8075d
  ```
  This is a **glibc-version-tracking constant**, not an algorithm divergence.
  Upstream `0x628` was correct for the glibc version they pinned against at
  commit time; our `0x620` is correct for our target glibc 2.39 (validated
  fresh this session — `lowfat-check-config` ⇒ `OK`). Every other byte in
  `lowfat_config.{c,h}` and `lowfat.ld` (sizes table, magics, sub-layouts,
  region offsets, linker script) matches both variants exactly.

  **Verdict:** clean on every byte that participates in the encoding, magic
  computation, or region layout. The single intentional FlexFat-side diff
  (`LOWFAT_JOINID_OFFSET`) is the one we keep tuned to the host glibc; treat
  our committed goldens as canonical for our build, upstream as canonical for
  the SPEC's structural invariants. The SPEC's file:line citations are
  presumed valid (see STATUS.md "REFERENCE pinning"; four citations
  spot-verified).

## ABI parity (recorded for later units; NOT exercised in Unit 1)
Keep byte-identical to the reference: the `lowfat_*` runtime symbol names; the
fixed tables `_LOWFAT_SIZES` @ `0x200000` and `_LOWFAT_MAGICS` @ `0x300000`;
`LOWFAT_REGION_SIZE_SHIFT = 35` (32 GiB regions); the `LOWFAT_OOB_ERROR_*` info
codes; and the exact `LOWFAT ERROR:` report text. FlexFat is x86_64-only; the
runtime is built with `-mcmodel=large -mbmi -mbmi2 -mlzcnt` to match the
reference. Unit 1 emits none of this — it is a no-op scaffold.
