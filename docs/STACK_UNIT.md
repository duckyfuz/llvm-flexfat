# Stack-protection unit — notes

Forward notes for the FlexFat stack-protection unit (SPEC §II.1). Originally
recorded as a blocked pre-unit during Unit 3; Unit 12a has landed the
runtime half and 12b will land the pass half + the MSET flip.

## Status

- **Unit 12a — runtime: SHM + per-class MAP_SHARED stack regions + pivot.**
  ✅ landed. Gate 51/51. See [STATUS.md](STATUS.md) "Unit 12a — stack runtime"
  for the full diff and the MAP_SHARED-fork caveat.
- **Unit 12b — pass: `doesAllocaEscape` + `makeAllocaLowFatPtr` + inlined
  stack helpers + `-flexfat-no-replace-alloca`.** ✅ landed. Gate 57/57.
  Escape-gated alloca lowfatification: escaping ⇒ low-fat (per the reference
  code, not SPEC's English wording at line 336). Inlined helpers per the
  Unit-7 architecture decision — no `addLowFatFuncs` helper-call path.
  Codegen parity verified: fast-path mirror is a single `leaq cst(%rsp)`,
  bounds check is `shr/table-load/single cmpq/jae`. See STATUS.md
  "Unit 12b" for full details and the MSET flip table.
- **Unit 13 (globals) + Part III (fork interposer, threads, dynamic loader)**
  are downstream and separate.

## ⚠ HISTORICAL: SHM support must land first (resolved by Unit 12a)
Stack mirroring cannot be built until the runtime has the shared-memory
machinery. Concretely it needs:
- **`lowfat_create_shm`** — create an anonymous, unlinked `/dev/shm` object
  (reference `lowfat_linux.c`: `O_EXCL` temp in `/dev/shm`, unlinked,
  `F_SETLEASE`, `ftruncate`), returning an fd.
- **`MAP_SHARED` same-fd region mapping** — each size-class stack region
  (`lowfat_stacks`) is `mmap`-ed `MAP_SHARED` to that one fd, so the *same
  physical bytes* are visible at every size-class address (SPEC §II.1,
  reference `lowfat.c:323-351`).

Unit 3's encoding-core init deliberately uses anonymous `mmap` + `mprotect` for
the SIZES/MAGICS tables and does **not** map stack regions or provide
`lowfat_create_shm` (see [STATUS.md](STATUS.md)). That shortcut is fine for the
tables but is exactly the mechanism stack mirroring requires, so **SHM support is
a hard prerequisite for this unit** — land it (as its own unit or the first step
here) before any stack work.

## ⚠ SPEC line 336 is INVERTED relative to the reference code
SPEC §II.1 line 336 reads:

> "Escape analysis (`doesAllocaEscape`, ≈1343-1414) leaves escaping allocas
> native (non-fat)."

**The reference code does the opposite.** `isInterestingAlloca`
(LowFat.cpp:1419-1430) returns true exactly when `doesAllocaEscape` returns
true, and only "interesting" allocas reach `makeAllocaLowFatPtr`. So:

> **An alloca is lowfatified iff `doesAllocaEscape(Alloca) == true` — i.e., its
> address can be observed outside direct-use channels (stored as a value,
> passed to a memory-touching call/invoke, ptrtoint that escapes, or
> recursively through gep/bitcast/select/phi). Allocas only used by load /
> cmp / self-store / return-of-local / lifetime intrinsics / pure-function
> calls stay native.**

This is the more sensible direction (the static-bounds analysis from Unit 8
already covers non-escaping allocas' direct accesses for free; lowfatifying
them too would only pay the mirror cost without gaining detection).
**Unit 12b follows the code.**

## What the unit then needs (SPEC §II.1, for later)
- Map the stack regions `MAP_SHARED` to the shm fd at init (the loop the Unit-3
  init skipped), plus the master stack region (`LOWFAT_STACK_REGION`).
- Pass side: `makeAllocaLowFatPtr` — sizeclass via `clzll(size)`, set alignment,
  replace oversized allocas, then `lowfat_stack_mirror(ptr, offset)` + RAUW.
- Stack tables `lowfat_stack_sizes/masks/offsets[]` are **already generated** in
  `lowfat_config.c` (Unit 2) — no generator work needed.
- Stack pivot (`lowfat_stack_pivot`) before `main`; escape analysis leaves
  escaping allocas native.
- Caveat (SPEC Part III): `MAP_SHARED` stacks also force `fork` interposition to
  avoid parent/child stack aliasing — track as a follow-on.

## MSET differential bugs to re-evaluate once this lands (Unit 11)
The Unit 11 MSET differential ([STATUS.md](STATUS.md) "Unit 11") deferred **48**
reference-detected bug types to this unit (heap-only ⇒ globals/stack not lowfat):
- **42 UNDETECTED** — Stack/Global-**origin** inter-object overflows: no check is
  inserted on a non-lowfat origin. They become checked once `makeAllocaLowFatPtr`
  / global lowfatification run.
- **6 PRECONDITIONS-FAILED** — Heap↔{Global,Stack} mixed pairs: today the
  high-lowfat-heap / low-normal-global address gulf breaks MSET's `target−origin`
  address-ordering precondition. The region sub-layout (`heap < global < stack`
  within every 2³⁵ region) makes that precondition satisfiable again once
  globals/stack move into their G/S sub-ranges, so these *become constructable* —
  re-run the differential and confirm parity (the reference detects all 6).

**Acceptance for the stack/global unit must include re-running the MSET
differential and reclassifying these 48** (expect most → DETECTED). Until then they
are deferred, *not* parity-confirming.
