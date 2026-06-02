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
| 6 | clang driver wiring: `-fsanitize=flexfat` (+ deprecated `lowfat` alias) recognized, x86_64-gated, forces `-mcmodel=large` + lzcnt/bmi/bmi2, links `libclang_rt.flexfat`, schedules the (no-op) pass at the reference's ScalarOptimizerLate point at every -O; e2e sentinel un-XFAILed | ✅ |
| 7 | load/store bounds-check instrumentation: `calcBasePtr` + inlined non-POW2 `lowfat_base` + inlined `lowfat_oob_check`; heap OOB traps with the exact report, in-bounds exits 0; **fast-path asm strategy-identical to the reference** (shr/table-load/single unsigned compare/`jae` to out-of-line error, no fast-path call, no div) | ✅ |
| 8 | static bounds analysis (`Bounds` lattice + `getPtrBounds`): provably in-bounds accesses (constant offset off known-size malloc/alloca/global, select/PHI merges, offset-0 input derefs) skip the check; genuine OOB / dynamic / unknown-provenance accesses still checked; Unit 7 traps still fire (no false negative); `-flexfat-no-check-fields` flag | ✅ |

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

## `-fsanitize=lowfat` deprecated alias & MSET safety (Unit 6, verified)
`-fsanitize=lowfat` is a deprecated alias for `-fsanitize=flexfat`
(`SanitizerArgs.cpp`, `parseArgValues`): it maps to the same `SanitizerKind`,
forces the same code model + features, and emits a stable **non-fatal** warning —
`argument '-fsanitize=lowfat' is deprecated, use '-fsanitize=flexfat' instead
[-Wdeprecated]`. Compilation still succeeds (exit 0). Pinned by
`clang/test/Driver/fsanitize-lowfat-deprecated.c` (real `-c` compile asserts
exit 0 + exact warning text + no `error:`, plus cc1 equivalence to flexfat).

**This does not break the MSET differential harness (Unit 11).** Checked the
committed configs `MSET/sanitizer_configs/lowfat_original.xml` and `lowfat.xml`:
- Their `compile_cmd`s carry **no `-Werror`** (only `-Wl,-T,after_text.ld` and
  `-g`), and the MSET evaluator injects none (`grep -r Werror MSET/src` is empty).
  So the `[-Wdeprecated]` warning stays a warning — the build does not fail. (If
  it *were* promoted via `-Werror`/`-Werror=deprecated`, it would become an error
  and break the compile; it is not, on this path.)
- MSET keys "bug detected" **solely on the run process's wait-status**
  (`src/evaluator/sanitizer.cpp:459-489`: `WIFSIGNALED → WTERMSIG`, else
  `WEXITSTATUS`) matched against `<bug_detected_exit_values>` (`6` = SIGABRT). It
  never inspects compiler stderr, and contains no stderr/`LOWFAT ERROR` text
  match. The deprecation warning is emitted at *compile* time on clang's stderr —
  a different process from the `<run>` step whose status is judged — so it cannot
  be misread as a bug-detection signal.
- As committed, both configs point `compile_cmd` at the **reference** LowFat clang
  (`llvm-lowfat/build/bin/clang` and `../sanitizers/lowfat/clang`), where
  `-fsanitize=lowfat` is native and *no* deprecation fires. The warning appears
  only if a Unit-11 config is re-pointed at *our* flexfat clang via the alias —
  and even then it is harmless per the two points above. A re-pointed config can
  simply use `-fsanitize=flexfat` to avoid the warning entirely.

## Load/store instrumentation & codegen parity (Unit 7)
The LOAD/STORE bounds check is implemented in `FlexFat.cpp`. Acceptance was met
(IR tests, trap/in-bounds e2e, fast-path asm parity), with these deliberate
divergences from the LLVM-4.0 reference — all justified, none on the fast path:

- **Inline IR instead of `call lowfat_base`/`lowfat_oob_check` + `addLowFatFuncs`.**
  The reference emits calls to alwaysinline helpers and relies on a bundled
  post-pass inliner to inline them. FlexFat is a New-PM *function* pass at
  ScalarOptimizerLate with no inliner after it (the Unit 6 decision), and a
  function pass cannot safely add module-level functions. So we emit the
  *post-inline* IR directly — the `lowfat_base` reciprocal-multiply and the
  `oob_check` body inline at the access site. The only out-of-line callee is
  `lowfat_oob_error`, in the cold error block, exactly as in the reference. Net
  effect on generated code is identical; instrumented objects just don't carry
  `lowfat_base`/`lowfat_oob_check` symbols (SPEC §5.3 lists those as pass-emitted
  — we inline them away; `lowfat_oob_error` is still referenced by name).

- **Branch weights: error edge weighted *cold* (`1:2000000000`) — intentionally
  INVERTED from the reference.** The reference weights the OOB (error) edge
  `2000000000` (hot) and relies on LLVM-4.0's noreturn-cold block-placement
  heuristic to override that for placement. **LLVM 23's `MachineBlockPlacement`
  honours the explicit branch weight over that heuristic** — so emitting the
  reference's direction verbatim puts the cold error block (and its
  `lowfat_oob_error` call) on the hot fall-through, a fast-path branch regression
  (observed in `-S`). We therefore invert the direction: the error edge gets the
  cold weight `1`, the fast edge `2000000000`. Same `2e9:1` magnitude/intent; the
  fast-path asm then matches the reference (`jae` to an out-of-line error block,
  fast path falls through to the access). This is pinned three ways so a future
  backend change cannot silently regress the hot path: `load.ll` / `store.ll`
  assert the IR weights in the emitted (error-edge-cold) direction and that the
  error block is the `!prof`-cold TRUE successor, and
  `clang/test/CodeGen/flexfat-error-block-placement.c` asserts at the asm level
  that the `lowfat_oob_error` call is emitted *after* the fast-path `ret`.

- **Base computation is non-POW2 (our default); the only built reference clang
  is POW2.** Side-by-side on the canonical `char get(char*q,int i){return q[i];}`:
  the *check* is identical (`shr $35`, `_LOWFAT_SIZES[idx]` absolute-addressed
  load, single `cmpq … , diff`, `jae` to the out-of-line error block, no
  fast-path call, no div). The *base* differs by **variant**: ours emits the
  non-POW2 reciprocal multiply (`mulxq` + `imulq`, using `_LOWFAT_MAGICS`@`0x300000`
  and `_LOWFAT_SIZES`@`0x200000`); the POW2 reference emits the bitmask `andq`.
  Both are div-free and inlined — "the original's choice per variant". Ours
  matches the SPEC §1.4 reciprocal and `lowfat.h`'s `lowfat_base`.

- **Cold-block call is large-model indirect** (`movabsq $lowfat_oob_error, %rax;
  callq *%rax`) vs the reference's direct `callq`, because the flexfat driver
  forces `-mcmodel=large` (required by LowFat's high addresses). This is in the
  cold error block only — not on the fast path. (The PIC/GOT setup at function
  entry seen under the default PIE vanishes with `-no-pie`; it is platform PIE
  overhead, not from the instrumentation.)

- **`access_size` defaults to 0** (check the byte at `ptr`).
  `-lowfat-check-whole-access` (`size = sizeof(access)-1`) and the §4.2 static
  bounds elimination of provably-safe checks are deferred. Without §4.2 every
  fat load/store is checked; alloca/global/constant bases are non-fat (NULL
  base ⇒ check dropped) since stack/global lowfatification is a later unit.

## Static bounds analysis (Unit 8)
`getPtrBounds` (port of LowFat.cpp:441-621) proves accesses in-bounds and skips
their checks: a pointer's `Bounds` is `[0, ub]` (max in-bounds byte offset), with
`NONFAT`/`UNKNOWN` sentinels; `run()` skips the check iff `isInBounds(0)` (the
`access_size = 0` default — `-flexfat-check-whole-access` is still deferred).
Measured win (`-flexfat-no-elide` toggles the analysis for A/B): the `bounds.ll`
sample drops **7 → 3** checks; a realistic `-O1` function drops 5 → 4. No false
negative: every Unit 7 trap test still aborts (verified).

Notes / opaque-pointer divergences:
- **`-flexfat-no-check-fields` is applied at the GEP, not in `getInputPtrBounds`.**
  The reference trusts an input pointer up to `sizeof(*ptr)`; opaque pointers have
  no pointee type, so we instead trust it up to the **GEP's source element type**
  size. This works on unoptimized IR (the `no_check_fields.ll` test), but is
  weaker at `-O1+`: the optimizer canonicalizes struct GEPs to `i8` GEPs, erasing
  the struct type, so field accesses fall back to checked. (Default — flag off —
  field accesses are checked regardless, matching the reference default.)
- **`getObjectSize` needs the allocation attributes.** Unlike LLVM-4.0's
  TLI-only recognition, LLVM 23's `getObjectSize` reads `allocsize`/`allockind`;
  clang emits them, so heap accesses size correctly. Hand-written test IR must
  carry them (see `bounds.ll`'s `malloc` declaration).
- **Input pointers are trusted at offset 0 (a detection/overhead tradeoff
  inherited from the reference).** Default input bounds are `[0,0]`, so a *direct*
  deref of an argument / loaded pointer / `inttoptr` / opaque-call result is
  elided; only positive offsets off such a pointer are checked. **Missed-bug
  class:** an already-out-of-bounds pointer passed across a function boundary and
  dereferenced *at its first byte* (`*p`, offset 0) is not caught — e.g. a caller
  forms `arr + 1000` for a 10-element array and the callee does `*p`. The
  instant a positive offset is applied (`p[k]`, `k>0`) the check returns.
  Verified by `unknown_producer.ll` (elided) vs its `arg_offset` contrast
  (checked). This is the reference's behavior; finding 3 confirmed it.
- **Truly-unrecognized producers default to `NONFAT` (elide) — but no longer
  silently.** Finding 3 confirmed `getPtrBounds` initializes `nonFat()` and the
  fall-through `else` leaves it there (LowFat.cpp:544 + :612-617), so an IR form
  the analysis doesn't recognize is elided. We keep that default for parity, but
  the fallback now emits a real (FileCheck-able) `(BUG) unknown pointer type`
  warning (port of the reference's `LowFatWarning`) **and** bumps a
  `NumUnknownProducers` statistic. **Missed-bug class:** an OOB access through a
  pointer from an unrecognized producer is missed — so this firing means our
  recognition list is incomplete for the IR we see (more likely on LLVM 23 /
  opaque pointers than on 4.0). `check-flexfat` asserts `NumUnknownProducers == 0`
  over the corpus: the IR canaries (`bounds.ll`, `unknown_producer.ll`,
  `--implicit-check-not="unknown pointer"`) and the e2e canary
  (`no_unknown_producers.c`, a producer-diverse `-O2` compile). Empirically 0
  fallbacks across the e2e and a varied real corpus (linked lists, atomics,
  C++ STL). `unknown_producer_diag.ll` is the negative control (an `atomicrmw`
  result trips it) proving the signal works. If the canary ever trips, the fix
  is to add that producer to `getPtrBounds`, not to ship a silent elision.

## Pass placement & the module→function decision (Unit 6, flagged)
The LowFat reference (LLVM 4.0) registered `createLowFatPass()` at
`EP_ScalarOptimizerLate` **+** `EP_EnabledOnOptLevel0` — i.e. as a per-function
transformation that runs right after mem2reg and stays visible to the rest of
the optimizer (LowFat *wants* its checks optimized — CSE'd, hoisted — which is
load-bearing for the performance-parity mandate), at every optimization level.

`EP_ScalarOptimizerLate` is a **function-level** extension point. The new-PM
analog, `registerScalarOptimizerLateEPCallback`, hands out a
`FunctionPassManager`, so the pass must be a **function pass**. Unit 1 stood
`FlexFatPass` up as a no-op *module* pass; Unit 6 converts it to a no-op
*function* pass (body unchanged) so it can occupy that slot faithfully. This is
correct for FlexFat specifically: its hot-path load/store checks only reference
the runtime-provided tables (`_LOWFAT_SIZES`@`0x200000`,
`_LOWFAT_MAGICS`@`0x300000`) as **externals**, so the per-function
instrumentation needs no module-level setup (unlike ASan, which builds its own
globals/ctors and is therefore a module pass at `OptimizerLast`).

Verified in this tree's `PassBuilderPipelines.cpp`: `buildO0DefaultPipeline`
invokes the ScalarOptimizerLate callbacks too, so the pass runs at `-O0` —
reproducing `EP_EnabledOnOptLevel0` without a separate registration. The pass
also declares `isRequired() = true` so the function-pass adaptor does not skip
it on the `optnone` functions clang stamps at `-O0`.
`clang/test/CodeGen/flexfat-pass-order.c` pins the placement: at `-O2`
FlexFatPass runs after `SROAPass` **and after the CGSCC `InlinerPass`**, and at
`-O0` it still runs.

**Inliner ordering (verified against the reference, not assumed).**
`EP_ScalarOptimizerLate` is a **post-(main-)inline** point in *both* PMs: in
legacy clang-4.0 (`PassManagerBuilder.cpp`) the main `Inliner` is added, then
`addFunctionSimplificationPasses` — which hosts `EP_ScalarOptimizerLate` — runs;
in the new PM the ScalarOptimizerLate callbacks fire inside the
function-simplification pipeline run by the CGSCC inliner wrapper. So FlexFat
runs *after* the main inliner — it is **not** a pre-inline pass. The reference's
`addLowFatPass` (clang-4.0 `BackendUtil.cpp:258-263`) additionally bundles a
**local** `createFunctionInliningPass()` right after `createLowFatPass()`
(comment: *"Inline LowFat instrumentation"*) whose only purpose is to inline the
`lowfat_base`/`lowfat_oob_check` helper **calls** the pass inserts, so the fast
path has no out-of-line call. **We do not reproduce that bundled inliner:** the
FlexFat instrumentation will emit its fast-path checks as **inline IR** (no
helper call — see the performance-parity mandate, "no out-of-line call on the
fast path"), which makes a post-pass inliner unnecessary by construction. If the
instrumentation instead grows out-of-line helpers, that inlining belongs in the
instrumentation unit (mark helpers `alwaysinline` + rely on the pipeline's
inliner, or emit inline IR) — *not* a pre-inline move of the pass.

**To revisit when the real instrumentation lands:** global *lowfatification*
(re-laying-out globals into lowfat regions) is genuinely module-scoped; if/when
that is ported it will be a **separate module pass** at a module extension
point, not a reason to move the hot-path function instrumentation off
ScalarOptimizerLate.

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
