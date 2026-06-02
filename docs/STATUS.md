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
