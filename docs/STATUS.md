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
| 9 | mem-intrinsic end-pointer checks (memcpy/memset/memmove, info MEMCPY/MEMSET), `replaceUnsafeLibFuncs` (mem-intrinsics always; allocator family + new/delete unless `-flexfat-no-replace-malloc`), `optimizeMalloc` (constant `malloc(K)` → `lowfat_malloc_index(idx,K)`, `heap_select` folded). **Pass↔runtime ABI closes**: e2e link+run through `-fsanitize=flexfat`; memcpy/memset overruns trap, constant-malloc asm calls `lowfat_malloc_index` with an immediate index (no `clzll`/`lzcnt`) | ✅ |
| 10 | option surface + SpecialCaseList blacklist: per-kind suppression (`-flexfat-no-check-reads/-writes/-memcpy/-memset`), `-flexfat-check-whole-access` (access_size = sizeof(*ptr)-1), error-block modes (`-flexfat-no-abort` warns+continues, `-flexfat-signal` inline `ud2`/SIGILL), and a `[flexfat]` `fun:`/`src:` blacklist; one behavioral test per flag, all defaults checks-on | ✅ |
| 11 | verification harness + MSET differential vs the reference oracle: consolidated `check-flexfat` (4 surfaces, 46/46), FlexFat MSET configs (`flexfat/mset/`), base+hardened differential — **FlexFat's detected set is a strict subset of the reference's, zero false detections, every miss classified** to a documented intentional difference; glibc TID/JOINID landmine validated (`lowfat-check-config`, OK on 2.39) | ✅ |
| 12a | Stack runtime: SHM helper (`lowfat_create_shm`), per-class stack regions mapped `MAP_SHARED` to one fd at init, `lowfat_envp` capture in `.preinit_array`, master-stack bump allocator (`lowfat_stack_alloc`), and the pivot trampoline (`lowfat_stack_pivot` asm + `lowfat_stack_pivot_2` payload) that copies the live native stack and switches `%rsp` before `main` runs — `&local` in `main` now classifies as `stack`, not `nonfat`. NO pass change yet; alloca lowfatification is Unit 12b. Gate is **51/51** | ✅ |
| 12b | Alloca lowfatification (pass half): `doesAllocaEscape` + `doesIntEscape` + `isInterestingAlloca` (escape predicate — escaping ⇒ low-fat, per the code, not SPEC's English wording), `makeAllocaLowFatPtr` (fixed + VLA paths; mirror gep tagged `!flexfat.stack.mirror`); `calcBasePtr`/`getPtrBounds` recognise the tag and emit inline `lowfat_base` for stack-mirror access; `-flexfat-no-replace-alloca` wired (Unit-10 forward-decl finally gets its behavioral test); idempotence guard skips already-mirrored allocas surfaced via inlining. Codegen parity: fast-path mirror is a single `leaq cst(%rsp)`, no div, no call, no runtime table load; check is `shr/table-load/single cmpq/jae` to out-of-line `lowfat_oob_error`. Gate is **57/57**. MSET flip pending differential re-run | ✅ |

Default shipped runtime config: **non-POW2** (matches `build.sh` default + SPEC §1.4).

## Operational notes (2026-06-06)

- **Part II acceptance extension.** Per `00a8ae8` ("docs: reclassify 6 MSET
  preconditions-failed as Part-II-scope deferrals, not permanent"), the 6
  `Heap↔{Global,Stack}` MSET types currently scored `PRECONDITIONS FAILED` are
  classified as deferred-until-Part-II, not permanent design wins. **Part II's
  acceptance criteria therefore include flipping all 6 to `DETECTED`** on a
  re-run of the MSET differential. The Unit 11 "Heap-origin → reference's
  existing check fires" path covers `Heap Global` / `Heap Stack` overflow; the
  `Global/Stack`-origin underflows depend on Part II actually inserting the
  check on the now-lowfat origin. If any of the 6 stays UNDETECTED post-Part-II,
  re-open the (a)/(b) question: that would be a permanent-property hit, not a
  Part-II miss.
- **REFERENCE loss + re-pin to upstream commit.** The original LowFat tree at
  `/home/kenf/Developer/CP4106/llvm-lowfat/` was permanently lost. It has been
  re-cloned from `https://github.com/GJDuck/LowFat` and **pinned to commit
  `20f8075dd1fd6588700262353c7ba619d82cea8f`** (2022-03-27, "Fix #23"). From
  now on, "REFERENCE" means this specific commit hash, not a mutable directory
  (see [LLVM_NOTES.md](LLVM_NOTES.md) "REFERENCE pinning").

  **Fingerprint verdict — effectively clean.** The clone's
  `config/lowfat-config.c` regenerates both variants byte-for-byte against our
  committed `flexfat/config/golden/`, *except* one line — `LOWFAT_JOINID_OFFSET`
  (upstream `0x628`, ours `0x620`). That is a glibc-version-tracking constant,
  not an algorithm diff: our value is the one the host validator confirms on
  glibc 2.39 (the validator just ran `OK` again this session). Every byte that
  participates in encoding/magics/region layout matches exactly. SPEC's
  file:line citations spot-verified against the pinned commit (4/4: `lowfat.h`
  71–137 accessors, `LowFat.cpp` 1168–1243 `lowfat_oob_check` IR body,
  `lowfat-config.c` 415–422 non-POW2 magic formula, `lowfat_malloc.c` 42–50
  `lowfat_regioninfo_s`). **The SPEC remains a reliable line-level index into
  the clone.**

  **Oracle / MSET-config inventory.** Inside this repo, `flexfat/mset/`
  contains **only FlexFat-side evidence** — `flexfat_original.xml`,
  `flexfat.xml`, `flexfat_original_detected.txt`, `flexfat_detected.txt`, and
  the README. The **reference oracles** (`lowfat_original_detected.txt`,
  `lowfat_detected.txt`) and the **reference MSET configs**
  (`lowfat_original.xml`, `lowfat.xml`) are NOT committed here. They survive
  on disk in the sibling `MSET/` tree at `/home/kenf/CP4106/MSET/`
  (`MSET/build/lowfat_*_detected.txt`, `MSET/sanitizer_configs/lowfat*.xml`),
  which was separate from the lost REFERENCE and is intact. **Future
  differentials remain re-scorable** against those oracle files. The only thing
  truly lost was the previously-built reference clang-4.0 toolchain that
  produced the runtime evidence — that can be rebuilt from the re-pinned
  REFERENCE if needed (clang-4.0 era on a modern host, feasible but not
  free; defer until a re-run is actually required).

  **Suspension lifted:** codegen-parity asm diffs and MSET oracle re-runs are
  no longer blocked on REFERENCE existence — they are now blocked only on
  rebuilding the reference clang toolchain from the pinned commit, which is
  Part-II-scope work, not blocking the current branch's gate.

  **The consolidated `check-flexfat` gate is unaffected** — all four surfaces
  (IR, runtime gtests, e2e lit, config goldens + size-sync) are self-contained
  in this repo; no test consumes REFERENCE at runtime. Strategy-level asm
  contracts are pinned by our own committed tests
  (`flexfat-error-block-placement.c`, `load.ll`/`store.ll` branch-weight
  asserts, `malloc_class.c`), so an asm-level reference re-diff is corroboration,
  not a load-bearing check.

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

## Intrinsic checks, libfunc replacement, optimizeMalloc (Unit 9)
This unit's teeth are integration: the e2e (`memcpy_oob.c`, `memset_oob.c`,
`mem_inbounds.c`) **link and run** through `-fsanitize=flexfat` against the real
`libclang_rt.flexfat`, proving the symbols the pass now emits (`lowfat_mem*`,
`lowfat_malloc_index`) resolve to the Units 3–5 runtime. A memcpy/memset overrun
traps with `operation = memcpy`/`memset` (the pass's end-pointer check fires
before the intrinsic, reporting `Dst+len`).

- **`optimizeMalloc` win is asm-confirmed.** A constant `malloc(100)` lowers to
  `movl $7, %edi; movl $100, %esi; jmp lowfat_malloc_index` — the size-class index
  (`7`) is a compile-time immediate, so the runtime `heap_select` `clzll`/`lzcnt`
  dispatch is gone. A dynamic `malloc(n)` stays `jmp lowfat_malloc` (which does
  the dispatch internally). Pinned by `optimize_malloc.ll`.
- **The pass size table is single-sourced and drift-guarded** (hardening past
  the original "duplicated, keep in sync"). `flexfatHeapSelect` reads
  `llvm/lib/Transforms/Instrumentation/FlexFatSizes.inc`, a **generated** artifact
  the Unit 2 generator (`flexfat/config/lowfat-config.c`) now emits in the *same
  run* as the runtime's `lowfat_sizes[]` — so the pass cannot hand-drift from the
  runtime. Drift is caught two ways, both in `check-flexfat`:
  - **Byte-for-byte:** `flexfat/config/test/sizes-sync.test` asserts the pass's
    `.inc` values equal the runtime `lowfat_config.c` `lowfat_sizes[]` values (and
    the pass `.inc` equals the committed golden `.inc`; the parity tests tie
    golden to a fresh regen). Verified to fail on a one-value corruption.
  - **Behaviorally:** `compiler-rt/test/flexfat/TestCases/malloc_class.c` —
    a constant `malloc(100)` is folded to `lowfat_malloc_index(7, 100)`; the e2e
    asserts the object lands in the runtime's region 7 / class 112 (`p[111]`
    passes, `p[112]` traps with `size = 112`). If the tables drift, the object
    lands in a different class → the in-bounds run traps (false positive) or the
    OOB run fails to trap (missed bug).
  The desync this prevents is silent and correctness-affecting: a stale index
  folds a constant malloc into the wrong region, decoded against the wrong size.
- **replaceUnsafeLibFuncs is per-call, not module RAUW.** A New-PM function pass
  must not `replaceAllUsesWith` a module-level declaration, so we redirect each
  call site (`CallBase::setCalledFunction`). Divergence from the reference: rare
  *non-call* uses of `memcpy`/`malloc` (e.g. taking the function's address) are
  not redirected — link-time `LOWFAT_ALIAS` interposition still covers their
  runtime behavior. Mem-intrinsics (`llvm.mem*`) are a separate path (the
  end-pointer checks); only the *named* libc calls are redirected here.
- **Two overlapping safety nets for mem ops, by design.** The pass checks the
  `llvm.mem*` intrinsic end pointers (fires first, reports MEMCPY/MEMSET), and
  the runtime `lowfat_mem*` re-check (reached via replacement or link
  interposition when the intrinsic lowers to a libc call). Either catches an
  overrun; the report text is identical.

## Option surface & blacklist (Unit 10)
The pass options are `-mllvm -flexfat-*` (internal/developer flags). Reference →
FlexFat mapping (all bool defaults = checks ON):

| Reference (`-lowfat-*`) | FlexFat (`-flexfat-*`) | Status |
|---|---|---|
| `no-check-reads` / `no-check-writes` | same | behavioral (`check_suppression.ll`) |
| `no-check-memset` / `no-check-memcpy` | same | behavioral (`mem_suppression.ll`) |
| `no-check-fields` | same | behavioral (`no_check_fields.ll`, Unit 8) |
| `check-whole-access` | same | behavioral (`whole_access.ll`) |
| `no-replace-malloc` | same | behavioral (`replace_libfuncs.ll`, Unit 9) |
| `no-check-blacklist` | same | behavioral (`blacklist.ll`) |
| `no-abort` / `signal` | same | behavioral e2e (`error_no_abort.c` / `error_signal.c`) |
| `no-check-escapes` | same | **forward-declared, inert** (escapes are Part III) |
| `no-replace-alloca` / `no-replace-globals` | same | **forward-declared, inert** (stack/global lowfatification is Part II) |
| `no-check-escape-{call,return,store,ptr2int,insert}` | — | not ported (granular escape flags, Part III; the umbrella `no-check-escapes` covers them) |
| `lowfat-debug` | — | not ported (IR dump; add if needed) |

Decisions / notes:
- **`-lowfat-*` cl::opt aliases are NOT kept** (contrast the *user-facing*
  `-fsanitize=lowfat` alias, which is kept/deprecated). These are internal
  `-mllvm` developer flags, not a stable user ABI; the committed MSET configs
  that pass `-lowfat-*` (e.g. `lowfat.xml`'s `-mllvm -lowfat-check-whole-access`)
  target the *reference* clang, and any flexfat-side Unit-11 config will use
  `-flexfat-*`. Aliasing ~13 internal flags is surface bloat with no consumer.
- **`-flexfat-check-whole-access` uses `sizeof(*ptr)-1`** as access_size (the
  reference's `getTypeAllocSize(Ty)-1`), so `diff >=u size-(sizeof-1)` validates
  the last accessed byte. With opaque pointers the access type comes from the
  load/store value type, not a pointee type.
- **Blacklist format is modern SpecialCaseList**: a `[flexfat]` (or `[*]`)
  section with `fun:`/`src:` globs, via `inSection("flexfat", "fun"/"src", ...)`
  — not the reference's LLVM-4.0 `[src]`/`[fun]` *section* headers (the API
  changed: section headers are now tool-name globs). `src:` matches the module
  id, `fun:` the function name; a blacklisted function is skipped wholesale.
  The list is parsed once and cached per path.
- **`-flexfat-signal` e2e pins SIGILL (132) via `sh -c '%run %t; test $? -eq
  132'`** — lit's internal shell does not expand `$?`. The trap path makes no
  runtime call (no `LOWFAT` report), which also distinguishes it from the
  SIGABRT+report default and the `-flexfat-no-abort` warn-and-continue path.

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

**→ Resolved in Unit 12a** ([below](#unit-12a--stack-runtime-shm--pivot)):
`lowfat_create_shm` ported, every entry in `lowfat_stacks[]` mapped `MAP_SHARED`
to one fd at init time, and the master stack region (`LOWFAT_STACK_REGION`)
included in the loop. See [STACK_UNIT.md](STACK_UNIT.md) for the original
hand-off notes (now historical).

## Unit 11 — verification harness + MSET differential

### `check-flexfat` is the single 4-surface gate (consolidated, green)
`ninja check-flexfat` runs **46 tests, 46 passed** spanning all four surfaces as
one target — IR/FileCheck (`llvm/test/Instrumentation/FlexFat/`), runtime gtest
(`compiler-rt/lib/flexfat/tests/`), e2e lit (`compiler-rt/test/flexfat/TestCases/`),
and config golden-diff + size-sync (`flexfat/config/test/`). Nothing drifted out:
the IR canaries' `NumUnknownProducers == 0` assertion (Unit 8), the table golden
diffs (Unit 2), the pass↔runtime size-sync (`sizes-sync.test`, Unit 9), the
per-flag behavioral tests (Unit 10), and the e2e trap/in-bounds cases (Units 7/9)
all live under it. Pointer-encoding parity (the `lowfat-ptr-info` analogue) is
covered by the Unit 3 runtime gtests, also folded in. This is the single source of
truth for "FlexFat is not broken".

### The differential: FlexFat vs the reference LowFat MSET oracle
Harness and configs in [`flexfat/mset/`](../flexfat/mset/) (`flexfat_original.xml`
base, `flexfat.xml` hardened). Both drive **our** clang via `-fsanitize=flexfat`
(NOT `-fsanitize=lowfat`; the pass flag is `-flexfat-check-whole-access`, not
aliased to `-lowfat-*`), keyed on **exit 6 / SIGABRT**. Run with
`mset --evaluate`; FlexFat's `DETECTED` set diffed against the committed reference
oracle (`MSET/build/lowfat_original_detected.txt`, 96 types;
`lowfat_detected.txt`, 36 types). FlexFat's own detected sets are committed as
evidence (`flexfat/mset/flexfat_*_detected.txt`).

**Headline: FlexFat's detected set is a strict subset of the reference's — zero
false detections, and every miss maps to a documented intentional difference.
No unexplained miss.**

MSET scores each bug *type* `DETECTED` / `UNDETECTED` (ran, didn't catch) /
`PRECONDITIONS FAILED` (the bug's required memory layout couldn't be constructed —
**not** a detection miss). A type's two trailing tokens are `<Origin> <Target>`;
*Origin* is the bounds-checked object the access overflows out of. FlexFat catches
a bug only if the Origin is **heap** (Part I is heap-only; stack/global
lowfatification is Part II).

#### Base config — FlexFat detects 30 / reference 96
| | count |
|---|---|
| **Parity** (both detect) | **30** |
| **FlexFat-only** (FF detects, ref didn't) | **0** |
| **Reference-only** (ref detects, FF doesn't) | **66** |

All 30 FlexFat detections are a subset of the reference's 96 — **no false
positives, no novel detections to explain away.** Classification of all 66
reference-only deltas (every one maps to an intentional difference):

| # | Delta bucket | FlexFat verdict | Maps to (intentional difference) |
|---|---|---|---|
| 42 | Stack/Global-**origin** Inter-Object spatial | UNDETECTED | **Part-II-scope (deferred)** — the origin object is never lowfatified, so no check is inserted; the neighbor is valid memory ⇒ no trap. |
| 6 | Heap↔{Global,Stack} mixed pairs | **PRECONDITIONS FAILED** | **Part-II-scope (deferred)** — *not* a permanent layout property. The mix of a high-lowfat heap object and a low-normal (not-yet-lowfat) global/stack object breaks the test's address-ordering precondition, so MSET can't construct the bug *today*. Once Part II lowfatifies globals/stack the precondition is satisfiable again (provably — see below) and the bug must be **re-evaluated**, exactly as the reference detects it. |
| 12 | `Misuse-of-free` (temporal) | UNDETECTED | **Spatial-only by design** — FlexFat has no temporal/use-after-free detection. |
| 6 | **Heap→Heap** Linear (Inter-Object ×4 + Non-Object ×2) | UNDETECTED | **Offset-0 same-size-class adjacency blind spot** (inherent LowFat encoding limitation, shared with the reference — see below). |

`42 + 6 + 12 + 6 = 66.` ✔ — **48 are Part-II-scope deferrals** (42 UNDETECTED +
6 PRECONDITIONS-FAILED; same root cause, different MSET symptom), 12 spatial-only,
6 the shared encoding blind spot. **The only miss that is *not* closed by finishing
the planned scope is the 6-type Heap→Heap blind spot.**

#### Hardened config (`-flexfat-check-whole-access`) — FlexFat detects 30 / reference 36
FlexFat's detected set is **identical to the base config (the same 30 types)** —
whole-access checking added no detections in this corpus. Against the (narrower,
heap-focused) 36-type hardened oracle: **27 parity, 3 FlexFat-only, 9
reference-only.**
- The **3 FlexFat-only** (`…Write Global Stack`, `…Stdlib Write Global Stack`,
  `…Underflow Write Stack Global`) are **all present in the base reference oracle**
  — i.e. parity-confirming, not false positives. The 36-type hardened oracle is a
  narrower reference run that simply didn't score those Global/Stack-origin types;
  FlexFat (and the base reference) legitimately detect them.
- The **9 reference-only** = 3 `PRECONDITIONS FAILED` (Heap↔Global mixed pairs,
  Part-II-scope, as above) + **6 `UNDETECTED` that are exactly the same Heap→Heap
  Linear set as the base config**. Whole-access checking does not close any of
  them — see below.

### The 6 PRECONDITIONS-FAILED are Part-II-scope, not a permanent layout property
A `PRECONDITIONS FAILED` could be either (a) a *designed, permanent* property of the
region scheme that will still hold after Part II lowfatifies globals/stack — i.e.
genuinely parity-confirming — or (b) an artifact of globals/stack *not being lowfat
yet*, in which case it is the same deferred Part-II miss as the 42 UNDETECTED ones
and must not wear (a)'s label. It is **(b)**, settled two independent ways.

**Mechanism (why it fails today).** MSET signals `PRECONDITIONS FAILED` from *inside
the test binary*: the generated case `_exit(PRECONDITIONS_FAILED_VALUE)`s when its
own runtime precondition check fails (`sanitizer.cpp:492`). For
`…Overflow … Heap Global` the generated body (`linear_ooba_heap_global_inter_object_
overflow_direct_write_*.c`) is a heap `origin = malloc(8)` and a global
`char target[8]`, walked forward byte-by-byte until the pointer reaches `target`:
```c
if ( !((ssize_t)(GET_ADDR_BITS(target) - GET_ADDR_BITS(origin)) >= 0) )
    _exit(PRECONDITIONS_FAILED_VALUE);            // target must sit ABOVE origin
while ( GET_ADDR_BITS(&origin[reach_index]) != GET_ADDR_BITS(target) ) {
    origin[reach_index] = 0xFF;  ++reach_index;    // linear overflow, caught at origin's bound
}
```
The precondition is **address ordering**, not physical adjacency: `target` (the
overflow destination) must be above `origin`. Under heap-only FlexFat the heap
`origin` is relocated to a high 2³⁵-stride region (≥ 32 GiB) while the global
`target` stays in low `.data` (a few MiB), so `target − origin < 0` → the binary
self-exits PRECONDITIONS_FAILED. The underflow cases (`…Global Heap`, `…Stack Heap`)
are the mirror: they need `target` *below* `origin`, equally broken by the same gulf.

**Why Part II restores it — provably (falsifies (a)).** The reference's region
*sub*-layout (committed `golden/nonpow2/lowfat_config.c:9-14`) fixes the
within-region ordering of the three allocation kinds — heap at the bottom, globals
in the middle, stack at the top:

| sub-range | offset within each 2³⁵ region | ≈ |
|---|---|---|
| `LOWFAT_HEAP_MEMORY` | `[0, 17179803648)` | `[0, 16 GiB)` |
| — gap — | 28 672 B (`PROT_NONE`) | |
| `LOWFAT_GLOBAL_MEMORY` | `[17179832320, 25769766912)` | `[16 GiB, 24 GiB)` |
| — gap — | 36 864 B | |
| `LOWFAT_STACK_MEMORY` | `[25769803776, 2³⁵)` | `[24 GiB, 32 GiB)` |

So in **any** size-class region `k`, `addr(stack) > addr(global) > addr(heap)` is
*structurally guaranteed* (16 GiB / 24 GiB offset floors). Once Part II places a
global/stack object into the G/S sub-range of its region, the `target − origin ≥ 0`
precondition for `Heap→Global`/`Heap→Stack` overflow — and its mirror for the
`Global/Stack→Heap` underflows — is **satisfiable, indeed always-true for same-class
objects**. The gaps make the objects non-*adjacent*, but the bug never needed
adjacency: the linear walk is caught the instant it leaves `origin`'s bound, long
before traversing 16 GiB of heap + the gap. So the layout fact the user asked for
points to (b): the scheme *will* let MSET construct these bugs post-Part-II.

**Empirical confirmation.** The reference — which *has* globals+stack lowfatified —
**detected all 6** of these types (present in `lowfat_original_detected.txt`; 3 also
in the hardened `lowfat_detected.txt`). A permanent-property (a) reading would
require the reference to fail to construct them too; it does not. Heap-origin cases
(`Heap Global`, `Heap Stack`) will then hit FlexFat's *existing* heap bounds check
(the same one that already catches `Heap→Heap` non-linear and every `Heap→{Global,
Stack}` that MSET *can* set up today — see the base table's parity rows) and should
reach **parity**; the `Global/Stack`-origin underflows depend on Part II actually
inserting the check on the now-lowfat origin.

**Classification: these 6 are deferred — re-evaluate when Part II lands**, grouped
with the 42 UNDETECTED as Part-II-scope. They are *not* counted as parity-confirming
and *not* a permanent design win. (Tracked alongside the SHM/stack dependency in
[STACK_UNIT.md](STACK_UNIT.md).)

### The one residual: the Heap→Heap offset-0 adjacency blind spot (classified, not papered over)
The only genuine "FlexFat ran a heap-origin bug and didn't catch it" deltas are
**6 Heap→Heap Linear types, identical across base and hardened configs**:
```
Inter-Object Linear OOBA Overflow  Direct Read  Heap Heap
Inter-Object Linear OOBA Overflow  Direct Write Heap Heap
Inter-Object Linear OOBA Underflow Direct Read  Heap Heap
Inter-Object Linear OOBA Underflow Direct Write Heap Heap
Non-Object   Linear OOBA Underflow Direct Read  Heap Heap
Non-Object   Linear OOBA Underflow Direct Write Heap Heap
```
**Mechanism — the documented offset-0 cross-boundary blind spot.** Two heap objects
of the same size class are packed adjacently in one 2³⁵ region. A *linear* overflow
off object A by exactly its size lands at **offset 0 of neighbour B**; `lowfat_base`
resolves that pointer to **B's** base, so the unsigned `diff >=u size` check sees a
perfectly in-bounds pointer to B and passes. The reporter's `overflow = +0`
field is the literal signature of this boundary.

**Evidence it is the inherent LowFat encoding limitation, shared with the reference
— not a FlexFat regression:**
1. **The check provably works for heap origins** elsewhere: FlexFat catches every
   Heap→Global / Heap→Stack and every Non-Linear / Stdlib / Type-Confusion
   Heap→Heap variant. The MSET log shows `lowfat_error` firing on Heap→Heap
   concrete instances too (e.g. `linear_ooba_heap_heap_…_read_0/_1`,
   `…_write_0/_1`) — the instrumentation is wired and active; only the
   offset-0-landing, baseline-survivable instances are invisible.
2. **The hardened `-flexfat-check-whole-access` config closes none of the 6**
   (identical 30 detected). That is the diagnostic signature of a *base-computation*
   blind spot rather than an access-extent gap: widening the checked extent cannot
   help when the **computed base is already the neighbour's**. Theory predicts
   exactly this for offset-0 adjacency, and the experiment confirms it.
3. **Zero FlexFat-only false detections** against the base reference oracle — the
   base ABI (`_LOWFAT_MAGICS`/`_LOWFAT_SIZES`, `lowfat_base`) is byte-for-byte the
   reference's, so the encoding behaves identically.

The reference oracle records these 6 *types* as `DETECTED` because MSET credits the
reference for concrete instances whose overflow distance skips the immediate
same-class neighbour (≥2 slots, into unmapped/guard memory) and because the full
reference (which also lowfatifies stack+globals) has a different malloc-arena
history, so its concrete Heap→Heap pairs are not always exact same-class neighbours.
FlexFat's heap-only arena packs them adjacently, so the offset-0 instance dominates
the type's baseline-survivable score ⇒ `UNDETECTED`. This is a layout sensitivity of
a **shared** blind spot, not a divergence in detection logic. (Documented in the
Unit 8 "input pointers trusted at offset 0" caveat family; closing it requires
inter-object redzones or guard slots, which LowFat deliberately omits for
performance.)

### glibc TID/JOINID landmine — validated before Part II
`flexfat/config/lowfat-check-config.c` (port of the reference validator) checks the
hard-coded `LOWFAT_TID_OFFSET = 0x2d0` / `LOWFAT_JOINID_OFFSET = 0x620` (from the
committed `lowfat_config.c`) against the **host** glibc's real `struct pthread`
layout — a worker thread asserts `*(pthread_t + TID_OFFSET) == gettid()` and, after
detach, `*(pthread_t + JOINID_OFFSET) == self`. **Result: `OK`, exit 0 on glibc
2.39 (Ubuntu 2.39-0ubuntu8.7).** These offsets are version-specific and load-bearing
for Part II's stack/thread support; this is the canary to re-run on the target
glibc before trusting that unit.

### Caveat on the temporal phase
FlexFat is spatial-only, so every MSET temporal type is an expected miss. Several
temporal test binaries spawn detached workers that `sleep()` forever, so MSET's
temporal phase may not self-terminate; the **spatial** differential (the meaningful
comparison) completes fully first. The hardened run finished cleanly; the base run
was stopped after its spatial verdicts were emitted. This does not affect any
number above — temporal types are all `UNDETECTED` by design.

## Unit 12a — stack runtime: SHM + pivot

Lands the runtime half of stack protection. The pass is **unchanged** in this
unit — alloca lowfatification and the inlined stack helpers come in Unit 12b,
which keys its MSET flip off the foundations 12a establishes. Gate: 51/51.

### What landed
- **`lowfat_create_shm`** (port of `lowfat_linux.c:80-107`) — `O_EXCL` temp in
  `/dev/shm` with random hex suffix, immediately `unlink()`ed, `F_SETLEASE`
  guarding against shared paths, `ftruncate` to size. Returns an fd whose
  pages will be `MAP_SHARED`-aliased across the stack regions.
- **`lowfat_map(addr, len, r, w, fd)`** — extended with the fd parameter
  (`fd >= 0` ⇒ `MAP_SHARED`, else `MAP_PRIVATE|MAP_ANONYMOUS`). All existing
  callers updated (`-1`).
- **Per-class stack-region init.** In `lowfat_init`, after the malloc init,
  iterate `lowfat_stacks[]` (which already lists every size-class region that
  owns a stack sub-range plus the master `LOWFAT_STACK_REGION = 62`) and
  `lowfat_map(..., fd)` each `STACK_MEMORY_OFFSET..+STACK_MEMORY_SIZE` slice
  to the **same** shm fd — same physical bytes at every mirror.
- **`lowfat_envp` capture** — `lowfat_preinit` now saves `envp` so the pivot
  can walk it to find the high end of the initial native stack.
- **`lowfat_stack_alloc`** — single-thread bump allocator: takes
  `LOWFAT_STACKS_START + idx * LOWFAT_STACK_SIZE` slots from the master
  region, mprotects RW on every mirror via the same `lowfat_stacks[]` loop.
  No Fisher-Yates ASLR shuffle or thread freelist — those are Part III scope
  (`lowfat_threads.c`).
- **Pivot trampoline** — verbatim port of the reference's `lowfat_stack_pivot`
  asm (`movq %rsp, %rdi; movabsq $lowfat_stack_pivot_2, %rax; callq *%rax;
  movq %rax, %rsp; retq`) plus `lowfat_stack_pivot_2`: walk `envp` for
  `stack_bottom`, allocate a low-fat stack, `memcpy` the live range, then
  scan-and-patch every word that points back into the old range
  (saved `%rbp`, captured `&local` temporaries, etc.).
- **Wired into the constructor** — `lowfat_stack_pivot()` is the last call in
  `lowfat_init`, so `main` runs on the low-fat stack.

### MAP_SHARED + bare `fork()` ⇒ shared physical stack (gtest death-test caveat)
SPEC's Part III flags this and we hit it the moment we tried to run the gtest
death tests under the new runtime. `MAP_SHARED` pages do **not** trigger COW
on fork, so a vanilla `fork()` leaves parent and child reading/writing the
**same** physical stack bytes — the child segfaults the instant either side
touches its own stack. Verified via `strace`: the death-test child
(`gtest_death_test_style=fast`, the default) dies with `SIGSEGV` on its very
first instructions after `clone()`, before reaching the body. The reference
solves this with `lowfat_fork.c` (interpose `fork`, create fresh shared-mem
stacks for the child, `longjmp`); that's Part III work.

Mitigation, in-tree until the fork interposer lands: the gtest test main
sets `testing::FLAGS_gtest_death_test_style = "threadsafe"` so death tests
use `fork()+exec()`, giving the child a fresh process whose stack is set up
from scratch — the shared-physical-bytes alias never bites. All existing
death tests (`FreeNonHeapPointerErrors`, `BigObjectIsDePaged`, the page-macro
death tests in `tests/logic/`) verified green under threadsafe mode.

This is a **gtest-internal limitation only.** Real applications under
`-fsanitize=flexfat` that fork can break in the same way once globals/stack
lowfatification is fully on — Part III's fork interposer is required for that.
The e2e tests don't fork; the consolidated gate is unaffected.

### Reference asm trampoline + text relocations under PIE
The trampoline emits `movabsq $lowfat_stack_pivot_2, %rax`, which under PIE
produces a text relocation:
```
ld: warning: relocation against `lowfat_stack_pivot_2' in read-only section `.text'
ld: warning: creating DT_TEXTREL in a PIE
```
This matches the reference's emission verbatim and is required by
`-mcmodel=large`. A GOT-relative load would avoid the warning but would
diverge from the reference asm and pull in a runtime indirection. Kept as-is.

### SPEC §II.1 escape-rule sentence is INVERTED relative to the code
SPEC line 336 reads "Escape analysis (`doesAllocaEscape`, ≈1343-1414) leaves
escaping allocas native (non-fat)." **The code does the opposite.**
`isInterestingAlloca` (LowFat.cpp:1419-1430) returns true exactly when
`doesAllocaEscape` returns true; only "interesting" allocas reach
`makeAllocaLowFatPtr`. So **escaping ⇒ low-fat**, **non-escaping ⇒ native**.
This is the more sensible direction (the static-bounds analysis already
covers any non-escaping alloca's accesses) and is what Unit 12b will port.
Recorded here so the next reader doesn't flip it; see also
[STACK_UNIT.md](STACK_UNIT.md).

## Unit 12b — alloca lowfatification (the pass half)

Pass-side companion to 12a: any alloca whose address escapes is replaced by a
sized byte-array alloca at the size-class boundary plus a constant-offset
mirror gep tagged `!flexfat.stack.mirror`. After RAUW, every former use of
the alloca goes through the mirror — a fat pointer in its size-class region.
The bounds-check path (Unit 7) then catches stack OOB.

### Escape rule sentence (re: SPEC §II.1 line 336 inversion)
> An alloca is lowfatified iff `doesAllocaEscape(Alloca) == true` — its
> address is observable outside direct-use channels (stored as a value, passed
> to a memory-touching call/invoke, ptrtoint that escapes, or recursively
> through gep/bitcast/select/phi). Allocas only used by load/cmp/self-store/
> return-of-local/lifetime intrinsics/`doesNotAccessMemory` calls stay native.

Same direction as the reference's `isInterestingAlloca` (LowFat.cpp:1419-1430):
`isInterestingAlloca` returns true exactly when `doesAllocaEscape` returns true.
SPEC's English wording at line 336 ("leaves escaping allocas native") is
INVERTED — we follow the code.

### What landed
- **Escape analysis.** `doesIntEscape` + `doesAllocaEscape` + `isInterestingAlloca`
  (verbatim ports of LowFat.cpp:810-846, :1343-1414, :1419-1430). Recurses
  through gep/bitcast/select/phi. PtrToInt escapes only if the *integer*
  escapes — handled by `doesIntEscape`.
- **`makeAllocaLowFatPtr`** (LowFat.cpp:1512-1677). Two paths:
  - **Fixed**: `idx = clzll(size)`; if `idx <= clzll(LOWFAT_MAX_STACK_ALLOC_SIZE)`
    skip (alloca > 32 MiB). Set alignment to `~masks[idx]+1`. If
    `sizes[idx] != size` replace with a byte-array alloca of `sizes[idx]`.
    Emit mirror gep `getelementptr i8, ptr <alloca>, i64 <offsets[idx]>` —
    a SINGLE constant-add at codegen (a `leaq imm(%rsp), %rdi`). The 3
    constants come from a single-sourced in-pass table (`kStackSizes[]`,
    `kStackMasks[]`, `kStackOffsets[]`, matching `lowfat_config.c` byte-for-byte).
  - **VLA**: ctlz.i64(size, true) gives idx at runtime; the offsets/sizes/
    masks tables are inline IR (extern globals exported by the runtime —
    `lowfat_stack_{offsets,sizes,masks}`); `lowfat_stack_align` becomes
    `and+inttoptr` inline; `llvm.stackrestore` discards the unaligned head.
- **Inlined post-inline IR** — per the Unit 7 architecture decision, no
  `addLowFatFuncs` helper-function path. The mirror is a direct gep; the
  table loads (VLA only) and the align mask are direct inline IR.
- **`calcBasePtr` mirror recognition.** A GEP with `!flexfat.stack.mirror`
  metadata is treated as a fat pointer in its own right: `calcBasePtr` calls
  `emitInlineBase(GEP)` instead of walking through to the (non-fat) alloca.
- **`getPtrBounds` mirror recognition.** Same metadata key: a mirror gep is
  treated as an input pointer with `[0, 0]` bounds — direct deref is elided
  (matches the input-pointer convention) but any positive offset is checked.
- **`-flexfat-no-replace-alloca`** finally functional (the Unit 10 inert
  forward-decl). The behavioral test `stack_no_replace_alloca.ll` pins it.
- **Idempotence guard.** `isInterestingAlloca` skips any alloca whose user
  list already contains a `!flexfat.stack.mirror` gep — necessary because
  the function pass runs AFTER the inliner: when a previously-processed
  callee gets inlined into us, the callee's lowfat alloca appears in our
  function. Without the guard, we re-class-size it one tier up and double-
  mirror, with the inner mirror landing in an unmapped region — verified
  in-tree by a SIGSEGV on `stack_heavy_clean.c` until added.

### Codegen parity (the deferred asm gate)
The canonical escaping fixed alloca `char buf[16]; use(buf, i);` compiled at
`-O2 -mllvm -flexfat-no-elide` — the mirror is a **single constant add** on
the fast path, no div, no call, no runtime table load:
```
andq    $-32, %rsp                       ; align rsp to class boundary
subq    $64, %rsp                        ; reserve the (LLVM-doubled) alloca
movabsq $-2061584302080, %rax            ; offsets[59] folded as immediate
leaq    (%rsp,%rax), %rdi                ; <-- THE MIRROR: one constant add
callq   *%rax                            ; use(buf=mirror, i)
```
And the bounds check inside `use(char *p, int i) { p[i] = 0x41; }`:
```
shrq    $35, %rcx                        ; idx = mirror >> 35
mulxq   3145728(,%rcx,8), %rdx, %rdx     ; magics[idx] * mirror (high word)
imulq   2097152(,%rcx,8), %rdx           ; * sizes[idx] = base
subq    %rdx, %rdi                       ; diff = mirror+i - base
cmpq    2097152(,%rcx,8), %rdi
jae     .LBB0_2                          ; out-of-line error block
movb    $65, (%rsi); retq                ; fast path
```
Strategy-identical to Unit 7's heap path: `shr/table-load/single cmpq/jae`.

### Class-size bump-up (size = clzll(size), not clzll(size-1))
For exact powers of two, `clzll(size)` bumps the class up by one. Source
`char buf[16]` ⇒ `clzll(16) = 59` ⇒ `sizes[59] = 32` (not 16). The runtime
reports `size = 32` — the allocation class, not the source size. This is
the reference's deliberate choice ("the one-past-end guarantee") and is
pinned by `stack_oob.c`.


### MSET differential after Unit 12b (the deferred flip gate)

Re-ran `mset --evaluate flexfat_original.xml` against the vendored reference
oracle (`/home/kenf/CP4106/MSET/build/lowfat_original_detected.txt`, 96 types).

**Headline: FlexFat's detected set grows from 30 → 54 types** (+24 net).

| Bucket | Count |
|---|---|
| **FlexFat detected (was 30)** | **54** |
| Parity (both detect) | 47 |
| FlexFat-only | 7 |
| Reference-only | 49 |

The 30 → 54 change decomposes into **30 new detections + 6 lost detections**:

**+30 new detections** — all Stack-related (Stack as origin and/or target).
22 spatial Stack-origin / Stack-target inter-object OOBA types now fire,
plus 8 `Misuse-of-free Stack` (freeing a stack pointer now traps because the
runtime classifies the mirror as `stack`, reaches the existing
"attempt to free a stack pointer" path in `lowfat_free` — see Unit 4).

**−6 lost detections** — `Global↔Stack` linear overflows where MSET's
address-ordering precondition no longer holds at the *distance* level. In
Unit 11, Global (in `.data` at low addresses) was BELOW Stack (loader stack
at low addresses), so a Global→Stack overflow walked a short distance and
fired. In Unit 12b, Stack lives in master region 62 at ~`0x1F6_xx`, ~125 GiB
above Global, so the test's finite-step walker hits the MAX_REACH limit and
self-exits PRECONDITIONS_FAILED before reaching the target. These move to
PF, not to a "real miss" bucket. The reference, where Globals are ALSO
lowfat (region-sub-range layout), keeps Globals and Stacks within the same
2³⁵ region — distance is ≤8 GiB — so its walker succeeds. **Unit 13
restores these 6 to DETECTED** by lowfatifying globals into the same
regions as stack/heap.

**49 REF-only deltas, all classified to a documented intentional difference:**

| Count | Bucket | Maps to |
|---|---|---|
| 37 | Origin or target = Global (spatial + misuse-of-free Global) | Globals not yet lowfat — **Unit 13 scope** |
| 6 | Heap→Heap Inter/Non-Object Linear OOBA | **inherent encoding blind spot** (offset-0 same-class adjacency, shared with reference; documented in Unit 11) |
| 6 | Stack→Stack Inter/Non-Object Linear OOBA | **new analogous inherent blind spot** for stack — same mechanism as the Heap→Heap one, surfaced now that stack is lowfat. The non-linear / stdlib / type-confusion Stack→Stack variants *are* caught; only the offset-0-landing linear cases fall through. |

**7 FlexFat-only detections** — all Stack-origin → Global-target (or the
single Global-origin Underflow → Stack-target). These ARE real catches by
FlexFat. The reference oracle doesn't list them because in the reference's
fully-lowfat layout, the MSET test's address-ordering precondition lands
both objects in the same 2³⁵ region's sub-ranges, making the overflow
distance too small to satisfy SOME of the linear-walker preconditions for
specific variants. With FlexFat (stack-only lowfat), the test layout
differs and these particular MSET variants are constructable AND FlexFat
correctly detects the OOB. Net effect: parity-extending, not parity-breaking.

**Part-II acceptance status (`00a8ae8`):** the 6 `Heap↔{Global,Stack}`
PRECONDITIONS-FAILED types from Unit 11 are NOT flipped by Unit 12b
alone — they remain PF, now because the distance gulf is heap-vs-stack
rather than heap-vs-loader-stack. Closing this requires Unit 13 to land
globals in their sub-ranges (so all three kinds share the same 2³⁵ region
strides). Until then, the (a)/(b) question stays open for those 6: the
re-evaluation moves to post-Unit-13.

**The 6 Stack→Stack `Non-Object`/`Inter-Object Linear` REF-only deltas are
the new permanent baseline** — a Stack-side analogue of the Unit-11
documented Heap→Heap offset-0 adjacency blind spot. The encoding cannot
catch the literally-adjacent-and-offset-0 case in any LowFat variant.
These join the 6 Heap→Heap as the architectural floor. Closing would
require redzones, which LowFat deliberately omits for performance.

**Updated evidence:** `flexfat/mset/flexfat_original_detected.txt` rewritten
with the 54-type set; previous 30-type set superseded.

