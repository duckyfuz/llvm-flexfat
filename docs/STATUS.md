# FlexFat — status

Living status of the FlexFat reimplementation. Branch `flexfat/reimplementation`,
LLVM 23-dev (see [LLVM_NOTES.md](LLVM_NOTES.md)). Footprint: [INTREE_TOUCHPOINTS.md](INTREE_TOUCHPOINTS.md).

## Units landed
| Unit | Scope | State |
|---|---|---|
| 1 | Scaffolding: no-op pass, stub runtime, config skeleton, 4 test surfaces under `check-flexfat` | ✅ |
| 2 | Config/table generator (byte-identical to reference, POW2 + non-POW2) | ✅ |
| 3 | Runtime pointer-encoding core: `lowfat_index/size/magic/base/buffer_size`, tables @ `0x200000`/`0x300000`, region reservation, constructor/preinit; codegen parity (POW2 `and`, non-POW2 `mulq`, no `div`) | ✅ |
| 4 | Heap allocator: per-class bump+freelist, lazy `mprotect` commit, big-object de-page, realloc/calloc/alignment-family/strdup, libc fallback, `LOWFAT_ALIAS` interposition; fast-path asm parity (no `div`, `clzll`→`lzcnt`, freelist LIFO, per-region mutex) | ✅ |
| 5 | memops (`lowfat_memset/memmove/memcpy`), the five classifiers + `lowfat_kind`, and the OOB reporter (`lowfat_oob_error/warning/check`); **reporter output byte-identical to the reference** (char-diff clean for overflow + underflow) | ✅ |

Default shipped runtime config: **non-POW2** (matches `build.sh` default + SPEC §1.4).

## Known divergences from the reference / caveats
- **Encoding-table init uses anonymous `mmap` + fill + `mprotect`, NOT the
  reference's `/dev/shm` fd-aliasing** (`lowfat.c` init; cf. reference
  `lowfat.c:249-294`). This is **fine for the SIZES/MAGICS tables**: they hold
  identical values at the ABI addresses and are read-only after init. The only
  difference is RAM — we commit ~128 KB instead of aliasing a single SIZE_MAX
  page across the high index range. Not an ABI difference.
- **`MAP_FIXED_NOREPLACE`** is used instead of the reference's `MAP_FIXED` (detect
  a stray mapping instead of clobbering it). Same effect when the address is free.
- **Sanitizer coverage of the allocator** splits into two:
  - *Full runtime under ASan — infeasible (inherent to the fixed-address layout).*
    lowfat's fixed regions (`i·2^35`, e.g. `0x800000000`) live inside ASan's
    shadow/gap address range, so reserving them under ASan fails with `EEXIST`
    ("failed to reserve region: File exists") — verified empirically, and not
    fixable with `protect_shadow_gap=0` (the high regions collide with real
    HighShadow). The reference LowFat shares this incompatibility (both demand
    fixed address layouts). The integration gtests therefore run without ASan;
    they include death tests that exercise the guard-page fault paths.
  - *Allocator bookkeeping under ASan/UBSan — covered.* The page-arithmetic
    macros, freelist node link/unlink and posix_memalign offset math live in
    `compiler-rt/lib/flexfat/lowfat_malloc_internal.h` and are exercised on
    ordinary (non-fixed-region) memory under host ASan+UBSan by the `FlexFatLogic`
    test (`compiler-rt/lib/flexfat/tests/logic/`, wired into `check-flexfat`).
    That catches the UB the criterion was meant to catch.

## OOB reporter & exit-code convention (flagged)
The `LOWFAT ERROR:` report text is load-bearing twice over (the e2e `// CHECK:`s
in `compiler-rt/test/flexfat/TestCases/` and the Unit-11 MSET differential), so
the reporter is a **verbatim** port and its uncolored output is **byte-identical**
to the reference — verified by a character diff against the reference `lowfat.o`
for one overflow and one underflow case (banner + fields, backtrace excluded as
it is inherently address-variable). ANSI coloring is emitted only on a TTY.

**Exit code: FlexFat keeps `abort()` → SIGABRT (134 = 128+6).** This matches the
reference (`lowfat_oob_error` → `lowfat_error` → `abort()`) and the MSET
`lowfat_original.xml` / `lowfat.xml` configs, which key "bug detected" on exit
6/SIGABRT. SPEC §5.5 notes a "FlexFat variant uses 1" — that is the separate MSET
`lowfat_2*.xml` configs driving a `-lowfat-mode` build (exit 1), a **future
FlexFat MODE, not Unit 5**. Decision: the default stays SIGABRT/6 for
reference + MSET-original parity; an exit-1 mode, if added, must be gated behind a
mode flag and its own MSET config — do not change the default silently.

## ⚠ Dependency: SHM is a hard prerequisite for the stack unit
The `/dev/shm` aliasing we skipped for the tables is **not** skippable for stack
protection. Stack mirroring (SPEC §II.1) requires `lowfat_create_shm` plus each
size-class stack region `mmap`-ed **`MAP_SHARED` to the same file descriptor**, so
the same physical bytes are visible at the size-class address (and a stack object
allocated in the master region can be "mirrored" into its class region for
`lowfat_base`/`size` to work). Unit 3 does **not** provide `lowfat_create_shm`,
does not map stack regions, and uses only anonymous/private maps.

**→ The stack unit depends on landing SHM support (`lowfat_create_shm`,
`MAP_SHARED` same-fd regions) first.** See [STACK_UNIT.md](STACK_UNIT.md).
