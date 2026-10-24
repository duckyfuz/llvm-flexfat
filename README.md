# The LLVM Compiler Infrastructure

[![OpenSSF Scorecard](https://api.securityscorecards.dev/projects/github.com/llvm/llvm-project/badge)](https://securityscorecards.dev/viewer/?uri=github.com/llvm/llvm-project)
[![OpenSSF Best Practices](https://www.bestpractices.dev/projects/8273/badge)](https://www.bestpractices.dev/projects/8273)
[![libc++](https://github.com/llvm/llvm-project/actions/workflows/libcxx-build-and-test.yaml/badge.svg?branch=main&event=schedule)](https://github.com/llvm/llvm-project/actions/workflows/libcxx-build-and-test.yaml?query=event%3Aschedule)

Welcome to the LLVM project!

This repository contains the source code for LLVM, a toolkit for the
construction of highly optimized compilers, optimizers, and run-time
environments.

The LLVM project has multiple components. The core of the project is
itself called "LLVM". This contains all of the tools, libraries, and header
files needed to process intermediate representations and convert them into
object files. Tools include an assembler, disassembler, bitcode analyzer, and
bitcode optimizer.

C-like languages use the [Clang](https://clang.llvm.org/) frontend. This
component compiles C, C++, Objective-C, and Objective-C++ code into LLVM bitcode
-- and from there into object files, using LLVM.

Other components include:
the [libc++ C++ standard library](https://libcxx.llvm.org),
the [LLD linker](https://lld.llvm.org), and more.

## Getting the Source Code and Building LLVM

Consult the
[Getting Started with LLVM](https://llvm.org/docs/GettingStarted.html#getting-the-source-code-and-building-llvm)
page for information on building and running LLVM.

For information on how to contribute to the LLVM project, please take a look at
the [Contributing to LLVM](https://llvm.org/docs/Contributing.html) guide.

## Getting in touch

Join the [LLVM Discourse forums](https://discourse.llvm.org/), [Discord
chat](https://discord.gg/xS7Z362),
[LLVM Office Hours](https://llvm.org/docs/GettingInvolved.html#office-hours) or
[Regular sync-ups](https://llvm.org/docs/GettingInvolved.html#online-sync-ups).

## FlexFat prototype

Build and test from the CP4106 workspace root:

```sh
scripts/flexfat/flexfat.sh pow2 configure build test
scripts/flexfat/flexfat.sh custom configure build test
```

Enable instrumentation with `-fsanitize=flexfat`. Spatial and temporal errors
are fatal.

| Setting | Values | Default |
| --- | --- | --- |
| `-fsanitize-flexfat-tbi` / `-fno-sanitize-flexfat-tbi` | Enable / disable temporal checking | Off |
| `-mllvm -flexfat-check-whole-access=` | `true`, `false` | `false` |
| `-mllvm -flexfat-share-contained-geometry=` | `true`, `false` | `true`; skipped at `-O0` |
| `-mllvm -flexfat-tbi-ordinary-loads=` | `true`, `false` | `false`; experimental |
| `-mllvm -flexfat-tbi-storage=` | `last-byte`, `prior-byte`, `shadow`, `packed-shadow` | `last-byte` |

TBI requires little-endian Linux AArch64 with 64-bit pointers and
`-fsanitize=flexfat`. Explicit `-mllvm -flexfat-tbi=true/false` overrides the
Clang TBI flags. Storage selection requires TBI; the final storage assignment
wins. The legacy `-mllvm -flexfat-tbi-last-byte` and
`-mllvm -flexfat-tbi-prior-byte` selectors remain supported; either selector
set to `false` selects `last-byte`.

| TBI storage | Supported builds | Generation location |
| --- | --- | --- |
| `last-byte` | POW2, custom | Final byte of the slot |
| `prior-byte` | POW2, custom | Byte immediately before the slot base |
| `shadow` | POW2 | Direct shadow; 16 TiB virtual reservation |
| `packed-shadow` | POW2 | Packed shadow; 512 MiB plus a sentinel page |

Build the compiler and runtime with matching size classes and TBI storage.
Set `-DFLEXFAT_SIZES_CFG=/path/to/sizes.cfg` at CMake configuration time for
custom classes; unset or empty selects POW2. Custom sizes must start at 16,
be strictly ascending multiples of 16, and pass the generator's geometry checks.

Managed allocations reserve a trailing byte and use size-class bounds, so
padding within a slot remains a detection blind spot. Default fixed-size
access checks test the starting address; `-flexfat-check-whole-access=true`
checks the full width. Memory intrinsics always check their full ranges.
Uninstrumented writes can corrupt in-slot generations. Oversized allocations
and exhausted regions fall back to the system allocator.

POW2 generations wrap modulo 256; custom generations cycle through 1–255.
There is no independent liveness tracking, so forged matching tags and
wraparound can evade temporal checks. POW2 shadow observations are ordinary
loads; in-slot observations are atomic unless ordinary loads are selected.
Custom observations use acquire atomics unless ordinary loads are selected.

Use `FLEXFAT_OPTIONS` for runtime settings, for example
`FLEXFAT_OPTIONS=exitcode=42:symbolize=false ./program`.

Run native temporal acceptance checks with:

```sh
python3 llvm-flexfat/compiler-rt/test/flexfat/run-temporal.py \
  llvm-flexfat/build-flexfat-pow2 --storage=last-byte
```

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.
