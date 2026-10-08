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
| `-mllvm -flexfat-alignment=` | `left`, `right` | `left` |
| `-mllvm -flexfat-check-whole-access=` | `true`, `false` | `false` |
| `-mllvm -flexfat-tbi=` | `true`, `false` | `false` |
| `-mllvm -flexfat-recover=` | `true`, `false` | `false` |
| `-mllvm -flexfat-tbi-storage=` | `shadow` (POW2 only), `last-byte`, `prior-byte` | `last-byte` |

The existing Clang TBI and sanitizer recovery flags remain supported. Explicit
`-flexfat-tbi` and `-flexfat-recover` settings override them.

The storage option requires `-fsanitize-flexfat-tbi`. The older
`-mllvm -flexfat-tbi-last-byte` and `-mllvm -flexfat-tbi-prior-byte`
spellings remain supported. Each storage assignment replaces the previous
one; either compatibility flag set to `false` selects `last-byte`.
In `last-byte` mode,
the generation occupies the final byte of each size-class slot. That byte is
excluded from usable capacity and runtime copies. Default scalar checks reject
accesses starting at the reserved byte; a wider scalar access starting before
it may overlap it. `-mllvm -flexfat-check-whole-access=true` checks the full
scalar width, and memory intrinsics always check their full range. One-past
pointers may point to the reserved byte. Last-byte and shadow objects have
different link ABIs and must be built with the same storage choice.
Uninstrumented code can overwrite an in-slot generation byte; use shadow
storage when such writes are
possible. Exercise the layouts with
`compiler-rt/test/flexfat/run-temporal.py BUILD_DIR --storage=last-byte`
(or `prior-byte`, or `shadow` for a POW2 build).

POW2 TBI shadow storage derives a generation address from the slot base:
`32 TiB + (slot_base >> 4)`. It reserves the zero-initialized 32–48 TiB
virtual address range at startup. Only the first 16-byte granule of each
slot stores its generation. Unmanaged pointers have a recovered base of zero,
which addresses a shared zero entry. The reservation commits physical pages
only as entries are written.

In `prior-byte` mode, each slot's generation occupies the byte immediately
before its base. The first aligned slot in every region is reserved so this
byte is mapped for the first allocation. The final byte of an allocated slot
may hold the following slot's generation and is excluded from usable capacity
and the start of default point-checked accesses. A wider scalar access may
overlap it unless whole-access checking is enabled. A zero-filled guard page
precedes the first region;
the reserved final byte of each later region provides a safe zero tag for its
unallocated first slot. Once a slot base is recovered, the tag address is
`base - 1`; recovering a base from an interior pointer still requires the
size-class geometry. This mode has its own link ABI.

FlexFat does not perform loop-specific geometry hoisting, loop versioning,
or affine-range grouping. Loop accesses use ordinary spatial and temporal
instrumentation. Proven contained-allocation geometry sharing and general IR
cleanup remain available.

POW2 uses generations 0–255 in every TBI storage mode. Fresh slots start at
zero, and free advances the generation modulo 256. Each covered access loads
the generation and compares it directly with the pointer tag. Shadow checks
use ordinary, non-volatile loads, like HWASan; later optimization may reuse or
hoist these observations when legal. In-slot checks retain relaxed atomic
loads. Allocator metadata updates remain atomic. Untagged foreign pointers
match the zero sentinel; tagged foreign pointers fail. Generation wraparound
can make an old tag match again,
and zero-tag pointers into never-allocated slots can pass the temporal check.
The POW2 ABI markers are `last_byte_pow2_v2`, `prior_byte_pow2_v3`, and `v7`
(shadow); objects compiled with older generation rules must be rebuilt.

Custom builds support last-byte and prior-byte storage. Metadata addresses
use the recovered slot base and class size, without a 1:16 shadow layout or
granule-alignment assumption. The existing size generator requires multiples
of 16 for allocation alignment. Custom generations remain 1–255, with the existing managed
region and zero-tag checks and partial-slot handling. Custom builds reject
shadow selection.

FlexFat instruments at OptimizerLastEP, then runs MemorySSA EarlyCSE,
InstCombine, and SimplifyCFG to simplify generated checks. At `-O0`, the
instrumentation still runs, but cleanup and contained-allocation geometry
sharing are skipped. Accesses removed by earlier optimization cannot be
checked. There are no mode, placement, or cleanup-selection flags.
Use `-mllvm -flexfat-alignment=right` to select right alignment independently.

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
