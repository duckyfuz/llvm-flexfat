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

This checkout contains the FlexFat pointer-bounds-checking sanitizer prototype.
When used in the CP4106 workspace, build and test it from the workspace root
with `scripts/flexfat/configure_llvm.sh`, followed by
`scripts/flexfat/run_flexfat.sh pow2` or
`scripts/flexfat/run_flexfat.sh custom`.

Use `-fsanitize=flexfat` to instrument an application. Controls:

| Setting | Values | Default |
| --- | --- | --- |
| `-mllvm -flexfat-mode=` | `fast`, `safe` | `fast` |
| `-mllvm -flexfat-alignment=` | `left`, `right` | `left` |
| `-mllvm -flexfat-check-whole-access=` | `true`, `false` | `false` |
| `-mllvm -flexfat-tbi=` | `true`, `false` | `false` |
| `-mllvm -flexfat-recover=` | `true`, `false` | `false` |
| `-mllvm -flexfat-tbi-storage=` | `shadow`, `last-byte`, `prior-byte` | `shadow` |

The existing Clang TBI and sanitizer recovery flags remain supported. Explicit
`-flexfat-tbi` and `-flexfat-recover` settings override them.

The storage option requires `-fsanitize-flexfat-tbi`. The older
`-mllvm -flexfat-tbi-last-byte` and `-mllvm -flexfat-tbi-prior-byte`
spellings remain supported. Each storage assignment replaces the previous
one; either compatibility flag set to `false` selects `shadow`.
In `last-byte` mode,
the generation occupies the final byte of each size-class slot. That byte is
excluded from usable capacity, copies, and instrumented memory accesses.
One-past pointers may point to it, but dereferencing it terminates even with
FlexFat recovery enabled. Last-byte and shadow objects have different link
ABIs and must be built with the same storage choice. Uninstrumented code can
overwrite an in-slot generation byte; use shadow storage when such writes are
possible. Compare the layouts with
`compiler-rt/test/flexfat/compare-temporal-storage.py BUILD_DIR`.

In `prior-byte` mode, each slot's generation occupies the byte immediately
before its base. The first aligned slot in every region is reserved so this
byte is mapped for the first allocation. The final byte of an allocated slot
may hold the following slot's generation and is excluded from usable capacity
and instrumented accesses. A zero-filled guard page precedes the first region;
the reserved final byte of each later region provides a safe zero tag for its
unallocated first slot. Once a slot base is recovered, the tag address is
`base - 1`; recovering a base from an interior pointer still requires the
size-class geometry. This mode has its own link ABI.

Fast mode instruments at ScalarOptimizerLateEP. Safe mode additionally
instruments at PipelineStartEP. Placement is internal and has no command-line
option. Alignment is independent of mode; for example, combine
`-mllvm -flexfat-mode=safe -mllvm -flexfat-alignment=right`.

Set `FLEXFAT_SIZES_CFG` to select a custom size-class configuration and
`FLEXFAT_OPTIONS` to provide runtime options such as `exitcode=6`.

The LLVM project has adopted a [code of conduct](https://llvm.org/docs/CodeOfConduct.html) for
participants to all modes of communication within the project.

### FlexFat heap slot policy

Managed heap allocations reserve at least one byte after the requested object.
Pointer escapes at or beyond the allocation slot boundary report an error;
legal requested-object one-past pointers remain inside the slot. Bounds are
still recovered from addresses, without pointer metadata. Padding within a
slot remains a spatial detection blind spot.

Class selection uses the normalized request plus one byte, and aligned
allocations additionally reserve worst-case alignment padding. Exact class-size
requests therefore move to the next class (doubling slot size in POW2).
The largest managed ordinary request is the largest class minus one byte;
alignment padding can reduce that limit further. Larger requests and exhausted
regions use matched system allocation/free fallback. Right alignment rounds
the offset down while retaining the trailing byte and malloc alignment.

Rebuild the compiler and runtime together when changing this policy. Previously
linked exact-fit runtimes do not satisfy the strict escape check invariant.
