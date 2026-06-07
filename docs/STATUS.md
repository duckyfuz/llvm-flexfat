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
| 13 | Global lowfatification: `isInterestingGlobal` + `makeGlobalVariableLowFatPtr` as a NEW **module pass** (`flexfat-globals`) registered at PipelineStart so sectioning happens BEFORE the function-level pass needs to see it. Eligible globals get `section "lowfat_section_<size>"` (or `..._const_<size>`) at the class-boundary alignment; Common→WeakAny promotion lets the linker honor the section attribute. Driver wiring: `-T <resource>/lowfat.ld` + `-z max-page-size=0x1000` on every flexfat link, plus the suppress-default-PIE shim in `Gnu.cpp` (lowfat.ld pins to absolute addresses, PIE relocates them). `calcBasePtr`/`getConstantPtrBounds` recognise lowfatified globals via the section name and emit inline `lowfat_base` so the Unit-7 check fires on global-derived pointers. `-flexfat-no-replace-globals` (Unit-10 forward-decl, last of three) finally functional. Gate is **64/64** | ✅ |
| 14a | Threads + build gate: `pthread_create` interposer (`dlsym(RTLD_NEXT, …)`) allocates a lowfat slot via `lowfat_stack_alloc`, sets it via `pthread_attr_setstack`, and pushes the slot to a reclamation freelist; `lowfat_is_thread_dead` reads TID/JOINID at the configured offsets to reclaim slots when their owner thread has joined (`tid==-1`) or detached + died (`tid==0 && joinid==thread`); Fisher-Yates shuffle of `lowfat_stack_perm[128]` ASLRs the slot pick order. **Build gate** — `flexfat_check_config` builds & runs the offset validator against host glibc at compile time; mismatch fails the build with named expected-vs-found offsets (negative control confirmed: corrupt JOINID → build fails with the exact message; restore → green). Fork interposer is Unit 14b — until then, the gtest threadsafe death-test mitigation from 12a stays. Gate is **71/71** | ✅ |
| 14b | Fork interposer: `clone(SIGCHLD)` onto a 4-page anonymous-shared temp stack with `setjmp`'d env at the top → child does (1) `lowfat_create_shm` fresh stack-memory object, (2) `mmap MAP_SHARED\|MAP_FIXED` over size-class 1's stack range to the fresh fd + `mprotect`+`memcpy` parent's live stack pages, (3) `pthread_cond_signal` parent on PROCESS_SHARED cond var, (4) loop remap of every remaining stack mirror to the same fresh fd, (5) `longjmp` back into `lowfat_fork()`'s setjmp frame on the now-private master stack. **12a MAP_SHARED fork hazard closed** (red→green: `fork_isolation.c` SEGSEGV'd at exit 139 against 14a runtime, exits 0 isolated under 14b); the 12a `gtest_death_test_style = "threadsafe"` mitigation is REVERTED (fast-mode death tests safe again, verified). Direct `clone()` callers remain unsupported (matches reference). Gate is **73/73** | ✅ |
| 15 | Escape checks at the 5 sites (lowfat.h:45-49): ESCAPE_CALL/RETURN/STORE/PTR2INT/INSERT. Each pointer-typed argument to a memory-impure call/invoke, each pointer-typed return value, each pointer-typed `store` VALUE, each non-trivially-escaping `ptrtoint` (whose source isn't an "ugly GEP", verbatim carve-out from LowFat.cpp:854-863), and each pointer-typed `insertvalue`/`insertelement` inserted operand gets a Unit-7-strategy bounds check before the site with the correct info code. Umbrella `-flexfat-no-check-escapes` (Unit-10 forward-decl) finally functional and joined by the five granular `-flexfat-no-check-escape-{call,return,store,ptr2int,insert}` flags Unit 10 skipped. e2e reports match the reference wording exactly (`operation = escape (call)` / `(return)` / `(store)`). No false positives on the prior 75-test suite. **MSET differential 96/96 — 100% parity with the reference oracle** (Unit 13's 78 → 96, +18 net, 0 lost). Trap-line audit: the 18 newly-detected types trap on a MIX of `operation = read` (Unit-7 access check, via `calcBasePtr` tracing the GEP to the origin alloca/malloc/global) and `operation = escape (call|store)` (Unit-15 escape sites). Unit 13's "architectural floor of 18" framing was wrong: the floor is a property of access-site checking WHEN `calcBasePtr` cannot trace to the origin, and the MSET corpus's GEPs are all compile-visible from the origin; Unit-13's TYPE-level UNDETECTED was caused by escape-only sub-cases that Unit 15 now closes. Gate is **85/85** | ✅ |
| 16 | Performance parity measurement. 5-benchmark synthetic micro-corpus at -O2 under `flexfat/perf/benchmarks/`; matrix = {uninstrumented, flexfat-full, flexfat-hardened} × {non-POW2} × N=10 runs, median + IQR. SPEC2006 not available; corpus is INDICATIVE not directly comparable. **Mean overhead non-POW2: full +5.6%, hardened +2.6%** (vs reference SPEC2006 ~64% / ~9.8%). Under the targets, not over — over-instrumentation hypothesis ruled out by STATISTIC counter audit (`NumChecks` ≤ 2 per benchmark, `NumUnknownProducers = 0`; Unit-8 elides aggressively on constant-bounded patterns). Notable: `heap_churn` runs **−19.5% FASTER** under FlexFat (lowfat_malloc beats glibc malloc on this churn pattern, matches reference README's same finding); `opaque_access` is the only benchmark that defeats Unit-8 elision and shows the cleanest hardened-vs-full delta (−10.5 pp). **POW2 attempted, surfaced a SIGSEGV correctness finding — root-caused and reclassified as Unit 17** (the project's true closing unit, which ports POW2 end-to-end and measures the POW2 matrix). Gate is **85/85** | ✅ |
| 17 | POW2 end-to-end port — closes Unit 16's audit finding. **Audit recorded**: every prior POW2 "parity" claim was (a) config/golden byte-diff or (b) encoding-arithmetic gtest; no test ever built+ran a POW2 binary. **Root-cause** (one sentence, gdb-traced): `optimizeMalloc` folds the constant-malloc `idx` host-side using a non-POW2-only `FlexFatSizes.inc`, producing `idx=45` for `malloc(65536)`; the POW2 runtime's 30-entry `LOWFAT_REGION_INFO` array doesn't service idx>30, so the next field load past the array end (`info->freelist` at +0x28) segfaults. **What landed**: single `LLVM_FLEXFAT_POW2` CMake option (default OFF) threaded through pass + runtime + lit; two committed per-variant pass tables (`FlexFatSizes_{nonpow2,pow2}.inc`) selected by `#if FLEXFAT_IS_POW2`; POW2 branch in `emitInlineBase` (single `and`); runtime variant select via `configure_file` of `lowfat_config.{c,h}` + `lowfat.ld`; gtest `#if !FLEXFAT_IS_POW2` gates for 4 non-POW2-only cases; lit feature `flexfat-{pow2,nonpow2}` with 13 `REQUIRES: flexfat-nonpow2` retrofits; one new POW2 e2e (`pow2_heap_boundary.c`: `malloc(63)` → class 64, `p[63]` OK, `p[64]` traps with `size=64`) — **first test ever to build and run a POW2 binary**. **POW2 perf matrix landed** (Unit 16's deferred row closed): full +4.6% mean, hardened +2.2% mean — ~1 pp under non-POW2, matching the reference's ~2 pp delta direction. Variant-aware gate: **86/85+1us non-POW2 default**, **82/69+13us POW2**, 0 failed either variant. The CLAUDE.md "support both variants" mandate is now end-to-end true | ✅ |

Default shipped runtime config: **non-POW2** (matches `build.sh` default + SPEC §1.4).

## Operational notes (2026-06-06)

- **Part II acceptance extension (CLOSED by Unit 13 + Unit 15).** Per
  `00a8ae8` ("docs: reclassify 6 MSET preconditions-failed as Part-II-scope
  deferrals, not permanent"), the 6 `Heap↔{Global,Stack}` MSET types
  originally scored `PRECONDITIONS FAILED` in Unit 11 were classified as
  deferred-until-Part-II, not permanent design wins. **Unit 13 closed
  this**: all 8 H↔{G,S} mixed-pair types flipped from PF → DETECTED via
  global lowfatification (criterion 3 in Unit 13's scorecard, 8/8 ✓).
  Separately, **the 18 same-class adjacency types Unit 11/12b/13 labelled
  "architectural floor / inherent encoding limitation" flipped to DETECTED
  in Unit 15** via escape-site instrumentation + already-firing
  `calcBasePtr` access checks — see the Unit-15 forward-pointers in §Unit
  11 / §Unit 12b / §Unit 13 and the corrected mechanism in §Unit 15 MSET.
  Net: every "deferred / permanent / architectural" framing in earlier
  STATUS sub-sections is now resolved (6 by Unit 13, 18 by Unit 15) and
  the honest residual is the narrower three-condition blind spot in §Unit
  15.
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
| 12 | `Misuse-of-free` (temporal) | UNDETECTED | **Spatial-only by design** — FlexFat has no temporal/use-after-free detection. **(SUPERSEDED by Unit 12b.** The 8 `Misuse-of-free Stack` types now DETECT via the Unit-4 `lowfat_free` classification path — calling `free` on a stack pointer reaches `LOWFAT ERROR: attempt to free a stack pointer detected!` once the stack pointer is in a lowfat region (12a SHM + 12b lowfatification). This is designed allocator-side coverage, not temporal detection; the spatial-only invariant still holds. Breakdown post-12b: **4 remaining `Misuse-of-free Global`** by-design-temporal-miss (no allocator classification for non-lowfat globals until Unit 13), **8 `Misuse-of-free Stack`** detected-by-classification. See "MSET differential after Unit 12b" below.) |
| 6 | **Heap→Heap** Linear (Inter-Object ×4 + Non-Object ×2) | UNDETECTED | **Offset-0 same-size-class adjacency blind spot** (inherent LowFat encoding limitation, shared with the reference — see below). |

`42 + 6 + 12 + 6 = 66.` ✔ — **48 are Part-II-scope deferrals** (42 UNDETECTED +
6 PRECONDITIONS-FAILED; same root cause, different MSET symptom), 12 spatial-only
(of which Unit 12b later converts 8 to detected-by-allocator-classification, 4
remain — see the Unit-12b MSET section below), 6 the shared encoding blind spot.
**The only miss that is *not* closed by finishing the planned scope is the
6-type Heap→Heap blind spot.**

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

**Unit-15 forward-pointer (don't re-read this section in isolation).**
The "lowfat_base resolves to neighbour's base" mechanism above is real
ONLY when the IR check uses runtime `lowfat_base(displaced_ptr)`. The
pass's `calcBasePtr` traces compile-visible GEPs to the origin
allocation, so when the displacement is GEP-derived in the same
function (which is the case for the MSET tests targeting these 6
types), the check uses `lowfat_base(origin)` instead, and traps. The
blind spot is real but narrower than this section implies: it requires
the displaced pointer to reach the check through a path `calcBasePtr`
cannot trace to the origin (typically an opaque function arg with
`getInputPtrBounds` defaulting to `[0,0]`). See the Unit 15 MSET
section for the trap-line audit and the three-condition residual.

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

**Carve-out (Unit 12b, retrospective).** The "every temporal type is an expected
miss" framing is **too coarse for `Misuse-of-free` specifically.** Calling
`free` on a non-heap **lowfat** pointer is caught by the Unit-4 allocator's
classification check (`lowfat_free` ⇒ `LOWFAT ERROR: attempt to free a stack
pointer detected!` for stack, "global" once globals lowfat in Unit 13). That
is **allocator-side input validation, not temporal detection** — the spatial-
only invariant is intact — but it is **designed coverage** for the
non-heap-pointer-to-`free` case, and it shows up in MSET's temporal phase as
`DETECTED`. With Unit 12a SHM + Unit 12b alloca lowfatification, this fires
on the 8 `Misuse-of-free Stack` types. Use-after-free / double-free remain
genuine spatial-only misses.

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

**Headline (full 3-axis metric, not just detected-delta):**

  - **54 detected** (was 30 in Unit 11; +24 net spatial coverage)
  - **6 newly-unconstructable** (Global↔Stack linear overflows regressed from
    DETECTED in Unit 11 to PRECONDITIONS_FAILED in 12b; see "Measurability
    regression" below — they are not "missed bugs," they are bugs MSET can
    no longer **construct** against our 12b layout)
  - **54 untestable-total** (full PF count in this run — includes the 6
    newly-unconstructable, the 6 Unit-11 PF mixed pairs that stayed PF, and
    additional Heap↔{Global,Stack} variants that became PF under 12b's
    expanded address gulf)

| Bucket | Count |
|---|---|
| **FlexFat detected (was 30)** | **54** |
| Parity (both detect) | 47 |
| FlexFat-only | 7 |
| Reference-only | 49 |
| **PRECONDITIONS_FAILED total** | **54** |
| ↳ of which **newly-unconstructable in 12b** | **6** |

The 30 → 54 change decomposes into **30 new detections + 6 lost detections**:

**+30 new detections** — all Stack-related (Stack as origin and/or target):

  - **22 spatial Stack-origin / Stack-target Inter-Object OOBA types** —
    real bounds-check fires through the mirror.
  - **8 `Misuse-of-free Stack`** detected via the **Unit-4 allocator's
    classification path**: calling `free` on a stack pointer reaches
    `lowfat_free`, fails `lowfat_is_heap_ptr`, classifies via
    `lowfat_is_stack_ptr`, and reports `LOWFAT ERROR: attempt to free a stack
    pointer detected!` (verbatim from the MSET run log; the actual error line
    is "`attempt to free a stack pointer detected!`" with `pointer = … (stack)`,
    `size = 256`, etc.). **This is allocator-side input validation, not
    temporal detection** — the spatial-only invariant is intact; it's a
    designed-but-previously-unreachable coverage class that lights up the
    moment stack pointers exist in a lowfat region. See the carve-out under
    "Caveat on the temporal phase" above. The remaining 4 `Misuse-of-free
    Global` stay UNDETECTED because non-lowfat globals fall through
    `lowfat_is_ptr` ⇒ `lowfat_fallback_free` ⇒ libc free; Unit 13 flips
    those 4 too via the same path once globals enter lowfat regions.

**−6 lost detections** — `Global↔Stack` linear overflows where MSET's
address-ordering precondition no longer holds at the *distance* level. In
Unit 11, Global (in `.data` at low addresses) was BELOW Stack (loader stack
at low addresses), so a Global→Stack overflow walked a short distance and
fired. In Unit 12b, Stack lives in master region 62 at ~`0x1F6_xx`, ~125 GiB
above Global, so the test's finite-step walker hits the MAX_REACH limit and
self-exits PRECONDITIONS_FAILED before reaching the target. **These are the
6 "newly-unconstructable" types** — they are not "missed bugs", they are
bugs MSET can no longer construct against our 12b layout. The reference,
where Globals are ALSO lowfat (region-sub-range layout), keeps Globals and
Stacks within the same 2³⁵ region — distance is ≤8 GiB — so its walker
succeeds. Unit 13 restores them to constructable; whether they then score
DETECTED, UNDETECTED, or stay PF must be re-evaluated, not assumed.

### Measurability regression (the 6 newly-unconstructable)
Unit 11 reported a "detected" delta; Unit 12b additionally introduces a
**measurability regression** that the detected-delta alone hides. The 6
Global↔Stack linear overflows that flipped from DETECTED (Unit 11) to
PRECONDITIONS_FAILED (Unit 12b) are NOT a coverage loss in the encoding —
the bounds-check logic still catches such accesses when they execute — they
are a coverage loss **in our ability to measure** it via MSET against our
current layout. Recorded here so the metric isn't single-axis. Unit 13 must
restore measurability AND then score them, whichever way they land.

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

**Unit-15 forward-pointer.** This "permanent baseline" framing is
superseded for the MSET corpus — all 6 flipped to DETECTED in
Unit 15, not via redzones but because the floor was being computed
on the wrong axis. See the Unit-15 MSET section: the encoding
blind spot applies only when `calcBasePtr` can't trace to the
origin; MSET's tests use compile-visible GEPs so the access-side
check uses the origin's bounds, and Unit-15 escape sites close
the residual escape-only sub-cases. The honest architectural
residual is narrower (three-condition residual in Unit 15).

**Updated evidence:** `flexfat/mset/flexfat_original_detected.txt` rewritten
with the 54-type set; previous 30-type set superseded.

### Unit 13 (globals) — acceptance criteria (recorded here, forward-looking)

The MSET differential gates Unit 13 needs to clear:

1. **Flip the 37 Global-related REF-only types** (origin or target = Global,
   spatial + the 4 `Misuse-of-free Global`): all 37 must move from
   UNDETECTED/PF to DETECTED. The 4 `Misuse-of-free Global` ride the same
   allocator-classification path that already lights up the 8 `Misuse-of-free
   Stack` in 12b (lowfat-classified non-heap pointer ⇒ `lowfat_free`
   classifies ⇒ "global pointer detected" error), and should flip
   automatically once globals enter lowfat regions.
2. **Restore measurability for the 6 newly-unconstructable Global↔Stack
   types from 12b.** Once globals lowfat-share regions with stack, the
   layout gulf collapses and MSET's distance precondition holds again.
   Acceptance = the 6 types are constructable AND then scored, whichever
   way they score (DETECTED is the expected outcome; if any stay UNDETECTED
   or remain PF, re-open the analysis — that would be a permanent property
   we hadn't surfaced).
3. **Re-evaluate the 6 Unit-11 `Heap↔{Global,Stack}` PF mixed pairs.** Per
   commit `00a8ae8`, they're classified as Part-II-scope deferrals. With
   globals in the proper sub-range, the within-region ordering
   `heap < global < stack` is structurally guaranteed, satisfying the test's
   address-ordering precondition. Same acceptance shape as item 2: must be
   constructable AND scored, not assumed DETECTED.
4. **No regression** on Unit 12b's 54-type detected set. The 22 Stack-origin
   /-target spatial detections and 8 `Misuse-of-free Stack` classifications
   must still fire. The 7 FlexFat-only catches may or may not survive
   (some depend on the stack-vs-global layout that Unit 13 changes —
   acceptable, expected).

The expected post-Unit-13 floor:
  - **6 Heap→Heap** + **6 Stack→Stack** Linear OOBA offset-0 adjacency
    blind spots (architectural; require redzones to close).
  - Plus whatever **6 Global→Global** analogous adjacency types surface as
    the same blind-spot class — flag during the Unit 13 differential, group
    with the others.

Anything outside that floor that doesn't flip is a Unit 13 bug, not an
architectural limit.

**Unit-15 forward-pointer.** The "expected post-Unit-13 floor of
18" framing was based on a wrong axis: it assumed the runtime
encoding blind spot would necessarily fire for MSET's tests of
these 18 types. Trap-line audit in Unit 15 shows the access-side
check (`calcBasePtr` → origin) was already catching half the
sub-cases; the TYPE-level UNDETECTED in Unit 13 came from
escape-only sub-cases. Unit 15 flipped all 18 to DETECTED without
redzones. See Unit 15 MSET section for the corrected floor
analysis.


## Unit 13 — global lowfatification

Pass-side companion to Units 12a/12b for the third memory kind. An eligible
global gets a `lowfat_section_<size>` (or `..._const_<size>`) section attribute
at its class-boundary alignment; the driver-applied `lowfat.ld` pins each such
section to its region's [16 GiB, 24 GiB) global sub-range; the runtime's
existing classifier (`lowfat_is_global_ptr`) then reports `&g` as `(global)`,
and the Unit-7 bounds check fires through `&g` like any heap/stack pointer.
Gate: 64/64.

### isInterestingGlobal predicate (verbatim from LowFat.cpp:1435-1458)
> A `GlobalVariable` is lowfatified iff: `-flexfat-no-replace-globals` is
> NOT set; it has NO user-declared section (`!hasSection()`); its alignment
> is ≤ 16 (`getAlign() <= 16`); it is NOT thread-local; AND its linkage is
> one of {External, Internal, Private, WeakAny, WeakODR, Common}.
> Additionally in `makeGlobalVariableLowFatPtr`: declarations are skipped;
> sizes >= `LOWFAT_MAX_GLOBAL_ALLOC_SIZE = 64 MiB` (idx ≤ 37) are skipped
> with a "too big" warning; Common-linkage globals are PROMOTED TO WeakAny
> before sectioning (the linker would otherwise drop the section attribute
> on Common symbols).

### What landed
- **Module pass `flexfat-globals`** registered at `PipelineStart`. The
  function pass (FlexFatPass) keys off the `lowfat_section_*` section name
  in `calcBasePtr` to recognise a global as fat, so the module pass MUST
  run first — verified during bring-up (registering at OptimizerLast left
  globals non-fat-from-FlexFatPass's-point-of-view and silently dropped
  the bounds check on every store-to-global).
- **Driver wiring** in `clang/lib/Driver/ToolChains/CommonArgs.cpp`:
  every flexfat link gets `-T <resource_dir>/lowfat.ld` and `-z
  max-page-size=0x1000`. lowfat.ld path is derived from the runtime
  archive's actual install location so it tracks per-target-runtime-dir.
- **Default-PIE suppression** in `clang/lib/Driver/ToolChains/Gnu.cpp`.
  lowfat.ld pins absolute high addresses (e.g. `0xbffff7000`); PIE
  relocates them, silently breaking global lowfatification on distros
  where PIE is the default (Rocky 10, modern Debian, etc.). The reference
  relied on its target distro defaulting to non-PIE; we explicit it.
  User-specified `-pie` still wins (would break globals — an explicit
  opt-out by the user).
- **Runtime install**: `compiler-rt/lib/flexfat/CMakeLists.txt` copies
  the generated `lowfat.ld` (from `flexfat/config/golden/nonpow2/`)
  into `${COMPILER_RT_OUTPUT_LIBRARY_DIR}/${triple}/` alongside
  `libclang_rt.flexfat.a`. Install rule mirrors the build-time copy.
- **`calcBasePtr` global recognition** — a `GlobalVariable` with a
  `lowfat_section_*` section is treated as fat (emit `emitInlineBase(GV)`
  instead of returning NonFat).
- **`-flexfat-no-replace-globals`** (last of the three Unit-10 forward-
  decls — `-no-replace-alloca` lit in 12b, this one in 13): now
  functional. `global_no_replace.ll` pins its behavior.

### Caveat: main executable only (SPEC §II.2)
Only globals in the main executable are lowfatified. Globals in shared
objects (.so) are ignored — the dynamic linker doesn't honor the
`lowfat_section_*` sections in shared objects, so any global declared
in a .so stays in `.data` / `.bss`, classified as `nonfat`, and the
bounds check is dropped. Documented; out of Unit 13 scope.

### MSET differential after Unit 13 (the five-criteria scorecard)

Re-ran `mset --evaluate flexfat_original.xml` against the vendored reference
oracle (96 detected types).

**3-axis metric (Unit 13):**
- **N = 78 detected** (was 54 in 12b; +24 net spatial+temporal coverage)
- **M = 7 newly-unconstructable in 13** (vs 12b) — all 7 are the 12b
  FF-only Stack↔Global catches that flipped to PF as globals-now-lowfat
  layout converged with the reference's, removing those specific MSET
  variants' constructability. NOT a "missed bug" — the encoding still
  catches such accesses; the test layout we can construct just no longer
  hits them.
- **P = 48 PF-total** (was 54 in 12b; net −6 — fewer types unconstructable
  overall because the Heap↔mixed-pair preconditions that broke in 12b
  are now constructable in 13).

**Headline counts vs reference oracle:**

| | |
|---|---:|
| FlexFat detected | **78** |
| Parity (both detect) | **78** |
| FlexFat-only | **0** |
| Reference-only | **18** |

**0 FlexFat-only — Unit 13 contains the parity strictly within the
reference's superset.** The 7 layout-fragile 12b FF-only catches that
flipped to PF in 13 explain why FF-only dropped from 7 → 0.

**The 18 REF-only deltas exactly match the predicted architectural floor:**

| Count | Bucket |
|---:|---|
| 6 | Heap→Heap Inter-Object / Non-Object Linear OOBA |
| 6 | Stack→Stack analogous Linear OOBA |
| 6 | Global→Global analogous Linear OOBA (NEW — surfaced as predicted in Unit 12b STATUS) |

All 18 are Linear OOBA Direct or Non-Object Linear OOBA — the offset-0
same-class adjacency cases. The non-linear / stdlib / type-confusion
variants ARE caught for all three kinds. The floor is the architectural
limit of LowFat-without-redzones, shared verbatim with the reference.

**Unit-15 forward-pointer — this whole sub-section is wrong on the
mechanism axis.** The 18 types were UNDETECTED in Unit 13 not
because the encoding's offset-0 adjacency blind spot fires on
them, but because each has escape-only sub-cases that need
escape-site instrumentation to trap. Unit 15 added the 5 escape
sites, flipped all 18 to DETECTED, and post-mortem trap-line
audit revealed the access-side check (`calcBasePtr` → origin) was
already catching half the sub-cases in Unit 13 — they just
weren't enough for MSET's per-type classifier. See the Unit 15
MSET section for the corrected mechanism (mixed `operation = read`
+ `escape (call|store)`, three-condition honest residual). The
"architectural limit of LowFat-without-redzones" claim above
applies only to the narrower residual; the 18 types do not.

### Five-criteria scorecard

| # | Criterion | Target | Result |
|---|---|---|---|
| 1 | Flip the 37 Global-related REF-only types from 12b (incl. 4 Misuse-of-free Global) | all 37 → DETECTED | **31/37 ✓** — 4 Misuse-of-free Global flipped via allocator-classification path (same as 8 Stack in 12b: `lowfat_free` ⇒ `attempt to free a global pointer detected!`); 27 spatial Global types flipped via the bounds check on lowfatified globals; the 6 that stayed UNDETECTED are exactly the Global→Global architectural-floor types (criterion 5). |
| 2 | Restore measurability for the 6 newly-unconstructable Global↔Stack from 12b, then score them | constructable + scored | **6/6 DETECTED ✓** — every one of the 6 specific 12b-PF Global↔Stack overflows is now constructable AND scored DETECTED. The within-region `heap < global < stack` sub-layout (SPEC §2) collapses the prior gulf, the walker satisfies its distance precondition, and the bounds check fires. |
| 3 | Re-evaluate the 6 Unit-11 Heap↔{Global,Stack} PF mixed pairs (`00a8ae8`) | constructable + scored | **8/8 DETECTED ✓** (the Heap↔{Global,Stack} mixed-pair family — 8 types in this oracle). All flipped from PF (Unit 11) → DETECTED (Unit 13). The Part-II acceptance from `00a8ae8` is fulfilled; the (a)/(b) question stays settled at (b) — these were Part-II-scope deferrals, not permanent properties. |
| 4 | No regression on Unit 12b's 54-type detected set | 54 still detected | **47/54 strict-survivor; 0 PARITY regression ✓** — 47 of the 54 12b detections survived. The 7 that didn't are EXACTLY the 7 12b FF-only catches (Stack↔Global linear) that the 12b STATUS flagged as "may or may not survive" — they flipped to PF as globals-now-lowfat layout converged with the reference's. That's "test variant became unconstructable," not "bug missed." None of the 47 parity-with-reference catches from 12b was lost. |
| 5 | Architectural floor | within floor | **18/18 = predicted shape ✓ (but mechanism mis-classified)** — REF-only deltas are exactly 6 H→H + 6 S→S + 6 G→G Linear offset-0 adjacency types as forecast. The "architectural floor" framing is wrong though: see Unit-15 forward-pointer above and the Unit 15 MSET section. These 18 are not encoding-blind-spot types; they are types whose per-type classification needed escape-site instrumentation to clear, and Unit 15 cleared all 18. The honest architectural residual (encoding blind spot + opaque-input + slot-multiple displacement) is narrower and not present in the MSET corpus. |

### Misuse-of-free temporal-phase carve-out — final reconciliation
Unit 11 listed 12 `Misuse-of-free` types as "spatial-only by design."
Unit 12b carved out 8 (Stack) as detected-by-allocator-classification,
leaving 4 (Global) by-design. **Unit 13 closes the carve-out: all 12 are
now detected via the same `lowfat_free` classification path** (globals
lowfat ⇒ `lowfat_is_ptr` true ⇒ `lowfat_is_heap_ptr` false ⇒ classifier
returns "global" ⇒ `attempt to free a global pointer detected!`). The
spatial-only invariant is still intact — this is allocator input
validation, not temporal detection. The reconciled count: **12 detected
via allocator classification (0 remaining by-design Misuse-of-free
misses).** Use-after-free / double-free remain genuine spatial-only
misses (also 0 detected on those in 13, as expected).

### Updated evidence
`flexfat/mset/flexfat_original_detected.txt` rewritten with the 78-type
set; 54-type Unit-12b set superseded.

### PF ledger (12b 54 → 13 48) — every transition named

The "PF total dropped from 54 → 48" headline hides the actual movement.
Derived from the committed evidence files plus the MSET run logs:

  - **LEFT PF: 15 types** (all → DETECTED in 13)
  - **ENTERED PF: 9 types**
  - **NET: 54 − 15 + 9 = 48** ✓

#### 15 types that LEFT PF (all → 13:DETECTED)

| Bucket | Count | Types | Mechanism |
|---|---:|---|---|
| **A. Canonical 11→12b "newly-unconstructable" restored** | 6 | `Inter-Object Linear OOBA Overflow Direct Read/Write Global Stack`; `Inter-Object Linear OOBA Overflow Stdlib Read/Write Global Stack`; `Inter-Object Linear OOBA Underflow Direct Read/Write Stack Global` | These are EXACTLY the 6 Stack↔Global Linear OOBA types Unit 12b lost to PF when Stack moved to master region 62 (~125 GiB above .data Globals). Unit 13's within-region `heap < global < stack` sub-layout collapses the gulf; walker satisfies its distance precondition; bounds check fires. **Criterion 2 fulfilled, verbatim.** |
| **B. Heap↔mixed PF actually present in 12b** | 3 | `Inter-Object Linear OOBA Overflow Direct Write Heap Global`; `Inter-Object Linear OOBA Overflow Stdlib Write Heap Global`; `Inter-Object Linear OOBA Underflow Direct Write Global Heap` | The ACTUAL Heap-mixed PF subset (ref-detected ∩ 12b-PF ∩ Heap-mixed). All flipped to DETECTED via the same sub-range layout fix. |
| **C. Non-Linear Global↔Stack rider** | 6 | `Inter-Object Non-Linear OOBA Overflow Direct Read/Write Global Stack`; `Inter-Object Non-Linear OOBA Overflow Stdlib Read/Write Global Stack`; `Inter-Object Non-Linear OOBA Underflow Direct Read/Write Stack Global` | Same constructability mechanism as bucket A but on the Non-Linear OOBA axis — Unit-11 didn't track these explicitly. They rode bucket A's fix unannounced. |

#### 9 types that ENTERED PF

| Bucket | Count | Types | Origin in 12b | Mechanism |
|---|---:|---|---|---|
| **D. 12b FF-only Stack-Global catches that flipped** | 7 | `Inter-Object Linear OOBA Overflow Direct Read Stack Global`; `Inter-Object Linear OOBA Overflow Stdlib Read Stack Global`; `Inter-Object Linear OOBA Underflow Direct Read Global Stack`; `Inter-Object Non-Linear OOBA Overflow Direct Read/Write Stack Global`; `Inter-Object Non-Linear OOBA Overflow Stdlib Read/Write Stack Global` | 12b:DETECTED | The 12b FF-only catches the 12b STATUS flagged as "may or may not survive — depends on layout." Globals-now-lowfat layout converged with the reference's, and these specific MSET test variants no longer satisfy preconditions. NOT a "bug missed" — the test variant is the thing that became unconstructable, not the encoding. |
| **E. NEW: Non-Linear Global→Stack Underflow** | 2 | `Inter-Object Non-Linear OOBA Underflow Direct Read Global Stack`; `Inter-Object Non-Linear OOBA Underflow Direct Write Global Stack` | 12b:UNDETECTED | Were UNDETECTED in 12b (Global was non-fat, no check fired). In 13: Global lowfat, target=Stack (within-region high), origin=Global (within-region middle). For Underflow, target<origin precondition required. target>origin → PF. Layout-collapse PF, not a regression in detection. |

#### Criterion-3 reconciliation — the "8/8" was an overcount

The scorecard claimed "8/8 Heap↔{Global,Stack} mixed pairs → DETECTED."
The recorded Unit-11 STATUS count was **6 PF**. The ledger forces this
to add up honestly:

  - My criterion-3 query enumerated **8 types** (Overflow×Direct R/W ×
    Heap-Global/Heap-Stack + Underflow×Direct R/W × Global-Heap/Stack-Heap).
  - Of those 8, only **2** were actually 12b-PF: `Heap Global Overflow
    Direct Write` and `Global Heap Underflow Direct Write` (both in
    bucket B above).
  - The other 6 query types were ALREADY DETECTED in 12b and should not
    have been counted toward "flipping PF → DETECTED."
  - The **canonical Heap-mixed PF subset in 12b is 3 types** (bucket B
    above), not 8 — my query missed `Heap Global Overflow Stdlib Write`
    and over-included 6 already-detected types.

**The Unit-11 STATUS "6 PF" claim itself doesn't match either.** Only 3
types actually fit the "ref-detected + 12b-PF + Heap↔(Global,Stack)
mixed" criterion. Unit-11 over-stated by 3 — likely conflating with the
Stack↔Global "newly-unconstructable" set (bucket A above), which has 6
types but is Stack-Global, not Heap-mixed. **Two readings reconcile**:

  - **Strict (per Unit-11's "Heap↔mixed" wording):** the canonical
    Heap-mixed PF subset is **3 types** (bucket B); 3/3 → DETECTED in 13. ✓
  - **Inclusive (treating "Heap↔mixed" as the broader 11→12b PF growth):**
    buckets A + B together = **9 types**; 9/9 → DETECTED in 13. ✓

Honest revised criterion-3 statement: **9/9 if measured by "all
11→12b PF growth" (buckets A+B); 3/3 if measured by the strict
Heap-mixed bucket Unit-11 named.** The original "8/8" measured neither
honestly — it was overbroad on the not-actually-PF side and undercounted
the actually-PF Stdlib-Write Heap-Global type. The Unit-11 "6" is itself
an artifact of imprecise classification at the time.

**Finding (not a rounding error):** Unit-11 STATUS's "6 PF Heap↔{Global,
Stack}" classification appears to have conflated two distinct buckets
(the actual 3-type Heap-mixed PF, plus the 6-type Stack↔Global "newly-
unconstructable" that 12b later named separately). The total movement
all-DETECTED in 13 either way, but the bucket labels deserved more
precision than they got.


## Unit 14a — threads + build gate

Two halves of the same unit:

### Build gate (TID/JOINID offset validation)

glibc declares `struct pthread` PRIVATE in `descr.h` and changes the
layout across versions without ABI notice. We've already observed two
correct JOINID values across glibc generations (0x620 on host glibc 2.39,
0x628 on the upstream LowFat pin's target glibc; see "REFERENCE pinning"
above). Dead-thread reclamation reads TID and JOINID directly at those
offsets; a wrong offset silently corrupts stack-slot ownership — the
runtime would reclaim a slot whose thread is still alive, then
`pthread_create` would hand the same stack to a new thread, two threads
would race over the same stack, and the corruption would surface as
arbitrary later crashes or silent OOB.

Unit 14a promotes `flexfat/config/lowfat-check-config.c` from a manual
check into a **build-time gate** wired into `compiler-rt/lib/flexfat/
CMakeLists.txt`. CMake builds the validator with the host C compiler
against the configured `lowfat_config.c`, runs it as a post-build step
whose success writes a stamp file, and pins the runtime archive
(`libclang_rt.flexfat.a`) as DEPENDS on that stamp. On mismatch the
validator prints expected-vs-found:

```
FlexFat build-gate failure: glibc pthread JOINID offset does not
match the configured constant.

  JOINID_OFFSET: configured 0x628
    value at that offset: 0x0
    expected value:        0x7fe4e4bcc6c0

glibc declares `struct pthread` layout PRIVATE in descr.h and
changes it without ABI notice (observed: JOINID 0x620 on glibc
2.39, 0x628 on the upstream LowFat pin's target glibc). …
```

The validator also fixes a race in the reference version: the original
worker `sleep(1)`s in a loop after TID-checking, so main can detach +
JOINID-check + print `OK` before the worker has actually run. Our
version uses a pthread cond var so the JOINID check is guaranteed to
happen after the worker's TID check.

The DEPENDS list of the validator binary includes BOTH
`lowfat-check-config.c` and the included `flexfat/config/golden/nonpow2/
lowfat_config.c` — without the second, a config edit doesn't trigger a
re-build of the validator and the gate silently keeps prior offsets
baked in. **Negative control verified end-to-end**: corrupting
`LOWFAT_JOINID_OFFSET` from 0x620 to 0x628 in the committed
`lowfat_config.c`, then `rm` the stamp, then `ninja flexfat_check_config`
fails with the named-offsets message; restoring 0x620 + re-stamping
passes. Build cycle ~2s.

### Threads (pthread_create interposition + reclamation + ASLR)

Runtime port of LowFat.cpp's `lowfat_threads.c`:

- **`lowfat_stack_perm[128]`** — Fisher-Yates permutation initialized in
  `lowfat_init` from `lowfat_rand` BEFORE the pivot, so the master
  thread's slot pick is already shuffled. Exposed (non-static) so the
  gtest can verify the permutation property without touching internals.
- **Freelist of dead-thread slots** — `lowfat_stack_alloc` walks it
  first; `lowfat_is_thread_dead` is the predicate. The freelist node
  lives in the last `sizeof(node)` bytes of the slot itself (no separate
  allocation).
- **Two final states for reclamation** (`tid==-1` ⇒ joined; `tid==0 &&
  joinid==thread` ⇒ detached + dead) — anything else is alive-or-zombie,
  the walker skips. Both reads are at the offsets the build gate just
  validated.
- **`pthread_create` interposer** — `dlsym(RTLD_NEXT, "pthread_create")`
  + `pthread_attr_setstack` to a fresh lowfat slot + push to freelist.
  Recovery: if real `pthread_create` fails AFTER we allocated the slot,
  `lowfat_force_stack_free` synthesizes a fake dead pthread_t at the top
  of the slot so the next walk reclaims.

### glibc-version deviations from the reference

The reference targets glibc 2.27 (pre-2.34, separate libpthread.so.0).
Notes for the post-2.34 host glibc we build against:

- **Symbol location**: post-2.34 glibc folded libpthread into libc.so.6,
  but the symbol is still `pthread_create` with the same signature.
  `dlsym(RTLD_NEXT, "pthread_create")` resolves it from libc instead of
  libpthread; behavior identical from the caller's perspective. No code
  change needed. Flagged here so future-us doesn't chase a non-issue.
- **`struct pthread` layout**: JOINID moved from 0x628 to 0x620 across
  the version range our runtime spans. The build gate is the response.
- **`lowfat_warning` on custom stacks**: SAME as the reference — if the
  user passes a pre-allocated stack via `pthread_attr_setstack`, we
  override with a lowfat slot and warn. Behavior unchanged.

### Fork interposer NOT in 14a (separate unit, 14b)

The bare-`fork()` MAP_SHARED hazard documented in 12a is NOT closed by
14a. Until the fork interposer lands (Unit 14b), tests that fork()
without exec() still alias parent/child physical stack bytes. Mitigation:
the gtest threadsafe death-test setting from 12a **stays** in
`flexfat_test_main.cpp`. Revert candidate moves to 14b's acceptance
checklist alongside the bare-fork e2e (red against 14a, green after 14b).

### What 14a leaves for 14b
1. `lowfat_fork.c` port (`clone(SIGCHLD)` on tmp stack → `lowfat_create_shm`
   per-class fresh stacks for child → `memcpy` parent stack → `longjmp`).
2. Bare-fork e2e: red against 14a, green after 14b. This is the empirical
   proof of the hazard 12a documented.
3. Death-test threadsafe revert (or document why it stays).
4. Known gap: direct `clone()` calls remain unsupported (matching the
   reference's choice).


## Unit 14b — fork interposer

Closes the MAP_SHARED stack-aliasing hazard 12a documented. Bare `fork()`
inherits parent's MAP_SHARED stack mappings; without interposing, parent
and child read/write the SAME physical stack bytes — verified
empirically: `compiler-rt/test/flexfat/TestCases/fork_isolation.c`
SIGSEGVs at exit 139 against 14a's runtime, exits 0 isolated under 14b.

### Sequence (port of LowFat.cpp's lowfat_fork.c)
1. Parent `mmap`s a 4-page `MAP_SHARED|MAP_ANONYMOUS` temp stack and
   places `lowfat_fork_info` (with `pthread_mutex_t`,
   `pthread_cond_t` both `PTHREAD_PROCESS_SHARED`, `jmp_buf`, and
   `__builtin_frame_address(0)`) at its high end.
2. Parent `setjmp(info->env)` then `clone(SIGCHLD,
   lowfat_fork_child_wrapper, stack_tmp_top, info)`.
3. Child on the temp stack:
   - `lowfat_create_shm(LOWFAT_STACK_MEMORY_SIZE)` — fresh fd, not seen
     by parent (different VM).
   - `mmap MAP_SHARED|MAP_FIXED|MAP_NORESERVE` over size-class 1's
     stack range to the fresh fd (replaces the parent-inherited mapping
     in CHILD'S address space only).
   - `mprotect` the slot's writable range RW, `memcpy` parent's live
     stack pages (from page-base-of-`info->stack` to slot top) into the
     fresh shm via size-class 1's mirror. Because all stack regions
     will MAP_SHARED to the same fd by the end, this one copy populates
     every mirror.
   - `pthread_cond_signal` parent; parent's `cond_wait` returns.
   - Loop over `lowfat_stacks[]` (every other size-class region incl.
     master 62), remap each to the fresh fd + mprotect.
   - `close(fd)`.
   - `longjmp(info->env, 1)` — `%rsp` and `%rip` restored to parent's
     setjmp call site; execution resumes on the now-private master
     stack region (which contains parent's content from step (3)'s
     memcpy via the shm aliasing).
4. Child returns 0 from `lowfat_fork()`; parent returns pid.

### What specifically breaks without each step
- Without (1) fresh shm fd: child's stack writes alias parent's (the
  exact 12a hazard — gtest fast-mode death tests SIGSEGV'd immediately).
- Without (2) mprotect+memcpy: child's pre-fork stack frame is
  unreadable (PROT_NONE inherited from parent's fresh-mmap setup) or
  zero-initialized; longjmp lands on garbage; child crashes.
- Without (3) cond_signal: parent hangs in `pthread_cond_wait` forever.
- Without (4) the per-class remap loop: any alloca-mirror access in the
  child after longjmp goes to parent-shared memory in the size-class
  mirrors that weren't remapped (Unit 12b lowfatification spreads
  writes across all mirrors).
- Without (5) longjmp: child has no clean return path — the temp stack
  would unwind through libc cleanup paths that touch parent state.

### Modern glibc deviations (post-2.34 unified libc) — every one flagged
- **`clone()`**: still in `<sched.h>` with the same signature; no change.
- **PROCESS_SHARED mutex/cond**: still functional via futex syscalls;
  the cond var lives in the temp stack (`MAP_SHARED|MAP_ANONYMOUS`) so
  parent and child see the same physical bytes.
- **fork() internal locks**: glibc's `fork()` takes malloc-arena +
  atfork locks. We interpose at the `fork` symbol with a `clone()`-based
  body, **bypassing those entirely**. Atfork handlers DO NOT run —
  matches the reference's deliberate choice. POSIX `fork()` callers that
  rely on atfork semantics (e.g. async-signal-safety for malloc state)
  must be careful; the deviation is documented here, not silently
  papered over.
- **Direct `clone()` calls**: NOT interposed. Programs that call
  `clone()` directly (without going through `fork()`/`pthread_create()`)
  inherit MAP_SHARED stacks and hit the original 12a hazard. Matches
  the reference's by-design gap.
- **`pthread_cond_wait` spurious wakeup**: the reference does a single
  `pthread_cond_wait`; we loop on `!info->done` because POSIX permits
  spurious wakeups (always has, though they're rare on Linux). Tiny
  hardening over the reference.
- **`__builtin_frame_address(0)`**: still works on GCC/Clang for the
  current frame; `LOWFAT_NOINLINE` keeps `lowfat_fork_wrapper` from
  inlining so the frame address is stable.
- **`returns_twice` on `fork`**: glibc's `<unistd.h>` declares it; the
  caller's IR carries the attribute even though our `lowfat_fork` impl
  doesn't list it (the attribute affects the CALLER's optimization
  scope, not the callee).

### Fast-mode death test mitigation REVERTED
12a's `flexfat_test_main.cpp` set
`testing::FLAGS_gtest_death_test_style = "threadsafe"` because bare
`fork()` SIGSEGV'd the gtest death-test child. 14b's interposer makes
fast-mode safe; the override is removed (verified:
`FlexFatMallocDeathTest.*` passes under fast mode). Bare-fork test
authoring is no longer constrained — gtests and e2e can call `fork()`
freely.

### Finding from 14b: volatile-alloca elision — investigated & fixed
**Status: root-caused, fixed, regression-test pinned.** Surfaced while
writing the fork e2e: under `-fsanitize=flexfat -O2`, a non-address-
escaping `volatile` local alloca's stores got eliminated. After the
user promoted this from "recorded" to "investigated before Unit 15",
the verdict:

1. **Reproducer pinned**:
   `llvm/test/Instrumentation/FlexFat/X86/volatile_alloca_escape_bug.ll`
   — pre-FlexFat-pass IR (as captured from `-print-after-all` on the
   minimal C program), now a CHECK'd regression test. Was XFAIL when
   first committed; fix landed in the same commit so the XFAIL was
   removed and the CHECKs are load-bearing.
2. **Bisect** (`clang -fsanitize=flexfat -O2 -mllvm -print-after-all`):
   - `FlexFatPass on main`: alloca is REPLACED by `alloca i8, i64 16`
     + mirror gep at offset `-2095944040448` tagged
     `!flexfat.stack.mirror`. Volatile load/store re-pointed at the
     mirror — they are still `store volatile` and `load volatile`.
   - `SROAPass on main` (4th run, late in the pipeline): the volatile
     stores and load are eliminated; main's body folds to
     `ret i32 poison`. Cause: SROA sees a stack alloca with accesses
     at a ~2 TB negative offset and treats the accesses as UB-on-
     dead-memory; `volatile` does not save them in that path.
3. **Classification: straight 12b pass bug, NOT upstream.** FlexFat's
   `doesAllocaEscape` (port of LowFat.cpp:1343-1414) treated
   `llvm.lifetime.start/end` intrinsics as escapes — they're
   `CallInst`s with argmem effects, so the `Call ⇒ doesNotAccessMemory
   else escape` branch returned true. clang `-O>=1` emits lifetime
   intrinsics on every alloca that survives mem2reg (and `volatile`
   forces survival), so the spurious-escape false positive turned
   into spurious lowfatification, which then turned into the SROA
   poison-fold downstream.
4. **Fix**: in `doesAllocaEscape`, add an `IntrinsicInst` carve-out
   that `continue`s on `Intrinsic::lifetime_start` / `lifetime_end`.
   Three-line change, ahead of the existing `CallInst` clause.
5. **Why the reference (LowFat 4.0) didn't trip it**: in clang/LLVM
   4.0 lifetime intrinsics were `doesNotAccessMemory()`, so the
   existing `Call` clause swallowed them. Modern LLVM marks them
   `memory(argmem: readwrite)`. Recorded so the next reader doesn't
   redo this bisect.
6. **Workaround removed**: `fork_isolation.c` no longer needs the
   `static unsigned int *volatile holder = &sentinel;` address-
   escape pin; the test was simplified back to the natural form, and
   passes (verified at -O0 and -O2).

**Senior-to-feature-work principle observed**: an instrumentation
tool that perturbs the semantics of the code it instruments has a
correctness hole senior to any feature work. The verdict landed
BEFORE Unit 15.

**Era-drift watch (one-liner for the next reader)**:
`doesAllocaEscape`'s `Call ⇒ doesNotAccessMemory else escape` clause
inherits clang/LLVM-4.0-era intrinsic memory-attribute assumptions;
any future "everything is suddenly lowfatified" perf regression
should suspect a new intrinsic falling through it. Companion test
pinning the category: `benign_intrinsics_no_escape.ll` (lifetime +
dbg.declare + assume(ptrtoint&N==0) all asserted non-escape). Grow
that test when adding a new intrinsic carve-out.

### Remaining known by-design gap
Direct `clone()` callers (programs not going through `fork()` or
`pthread_create()`) remain unsupported, matching the reference.


## Unit 15 — escape checks

Lands the five escape-site checks at the same level of the Unit-7
bounds-check pathway, gated by `filterKind` and consulting both the
umbrella `-flexfat-no-check-escapes` flag (Unit-10 forward-decl,
finally meaningful) and the five granular flags Unit 10 skipped
(`-flexfat-no-check-escape-{call,return,store,ptr2int,insert}`).
This unit makes the granular flags meaningful — the umbrella was the
only reason to skip them, so they're ported now alongside.

### Per-code sentences (one each, from the reference port)

1. **`ESCAPE_CALL` (info 5)** — at every `call`/`invoke` whose callee
   isn't `doesNotAccessMemory()`, every pointer-typed argument is
   checked at the call site with access_size=0 (the byte at the
   pointer); catches "I'm passing an OOB pointer to opaque code."
2. **`ESCAPE_RETURN` (info 6)** — at every `ret` whose returned value
   is pointer-typed, the returned pointer is checked at the ret with
   access_size=0; catches "I'm handing my caller an OOB pointer."
3. **`ESCAPE_STORE` (info 7)** — at every `store` whose VALUE operand
   is pointer-typed, the stored pointer is checked at the store with
   access_size=0; catches "I'm putting an OOB pointer somewhere
   readable." The store's DESTINATION pointer is a separate WRITE-kind
   check, not this one.
4. **`ESCAPE_PTR2INT` (info 8)** — at every `ptrtoint` whose result
   truly escapes (per `doesIntEscape`: reaches a store / call / invoke
   / inttoptr / ret) AND whose source isn't an "ugly GEP" (verbatim
   port of the LowFat.cpp:854-863 carve-out), the source pointer is
   checked at the ptrtoint with access_size=0; catches
   "I'm hashing/printing/leaking an OOB pointer's bits."
5. **`ESCAPE_INSERT` (info 9)** — at every `insertvalue` /
   `insertelement` whose inserted operand is pointer-typed, the
   inserted pointer is checked at the insert with access_size=0;
   catches "I'm packing an OOB pointer into an aggregate/vector that
   will be returned, stored, or passed onward."

### Granular flags reconciliation with Unit 10
Unit 10 forward-declared `-flexfat-no-check-escapes` as the umbrella,
and explicitly skipped the five granular `-flexfat-no-check-escape-*`
flags because the umbrella was inert. **Unit 15 ports all five
granular flags** alongside the now-meaningful umbrella; each granular
filterKind clause `return ClNoCheckEscape<Kind> || ClNoCheckEscapes;`
so setting the umbrella is equivalent to setting all granular flags.
The umbrella's first behavioral test is
`escape_umbrella_suppress.ll`; each granular flag's clause is
mechanically identical to the others (same one-liner pattern) so the
umbrella test plus per-kind escape tests cover the dispatch by
construction.

### The "ugly GEP" carve-out (verbatim port of LowFat.cpp:854-863)
`ptrtoint` whose source GEP is tagged with `!uglygep` metadata is
deliberately NOT instrumented for ESCAPE_PTR2INT, matching the
reference's false-positive avoidance. The metadata is set by
InstCombine in LLVM 4.0 when canonicalising a GEP into a byte-offset
form whose stride no longer matches the original element type, and
is rare on modern LLVM in practice. The carve-out is preserved
verbatim and pinned by `escape_ptr2int_ugly_gep.ll` — a falsifiable
port, not folklore: if a future change drops the metadata check,
the test starts emitting an escape check on the ptrtoint and fails.

### Era-drift watch: 4.0 enumeration vs LLVM 23 IR
Every 4.0-era escape site expresses identically on LLVM 23 IR:
  - `StoreInst` with pointer value: `store ptr %p, ptr %dst`.
  - `PtrToIntInst`: still valid; opaque pointers don't change shape.
  - `CallBase` with pointer args: still valid.
  - `ReturnInst` with pointer return: still valid.
  - `InsertValueInst` / `InsertElementInst` with pointer inserted
    operand: still valid (vector-of-ptr is `<N x ptr>`).
No site silently approximated — every check uses the same predicate
the reference did, with the same `getType()->isPointerTy()` test that
works identically under opaque pointers. The reference's
non-escape-site categories that we ALSO don't treat as escapes
(`AtomicRMWInst`, `AtomicCmpXchgInst` — they're WRITE-kind checks
in Unit 7, not escape) match the reference verbatim.

### Acceptance gates
1. **`check-flexfat` green, no false positives on prior tests.** All
   75 prior tests still pass — the escape checks added instrumentation
   but elided correctly on previously-clean pointers (input-pointer
   default `[0,0]` bounds → `isInBounds(0) == true` → escape elided).
   The 7 new IR tests (one per code + ugly-GEP carve-out + umbrella)
   and 3 new e2e tests (call/store/return with `operation = escape (…)`
   exact-match) bring the gate to **85/85**.
2. **MSET differential — 100% parity with the reference oracle (96/96).**
   Re-run completed against the same `flexfat_original.xml` corpus
   used in Unit 13:
   - **Detected**: 96 (Unit 13: 78; **+18 net, 0 lost**).
   - **FlexFat-only**: 0. **Reference-only**: 0.
   - **PF**: 48 (unchanged; escape sites don't shift preconditions).

   The 18 newly-detected types are exactly the Linear-OOBA
   same-class adjacency set Unit 13 STATUS labelled "architectural
   floor": 6 Heap→Heap + 6 Stack→Stack + 6 Global→Global, split
   {Inter-Object Overflow/Underflow × Direct R/W} ∪ {Non-Object
   Underflow × Direct R/W}.

   ### Trap-line audit — what is actually firing

   Parsed `operation = …` from every TC binary's trap in the
   re-run log. The breakdown is **mixed across two mechanisms**,
   not pure-escape:

   | Type bucket (6 each) | Sub-case mix per type |
   |---|---|
   | Inter-Object Overflow Direct **Read** (H/S/G) | 4× `read` + 4× `escape (store)` |
   | Inter-Object Overflow Direct **Write** (H/S/G) | 4× `escape (call)` + 4× `escape (store)` |
   | Inter-Object Underflow Direct **Read** (H/S/G) | 4× `read` + 4× `escape (store)` |
   | Inter-Object Underflow Direct **Write** (H/S/G) | 4× `escape (call)` + 4× `escape (store)` |
   | Non-Object Underflow Direct R/W (mixed) | ~100% `escape (call)` (Heap/Stack); mix for Global |

   So of the 18 types, the read variants (6) trap half on
   `operation = read` (Unit-7 access check firing) and half on
   `operation = escape (store)` (Unit-15 escape check firing); the
   write variants (12) trap entirely on escape codes 5/7.

   ### Mechanism — three corrections to the earlier "floor" story

   1. **The IR check uses the origin's bounds, not a recomputed
      base.** The runtime reporter's `base` field is whatever the
      pass passed as the third argument to `lowfat_oob_check`. For
      a Stack→Stack underflow Direct Read trap, the log shows
      `pointer = 0x…46f, base = 0x…470, size = 16, underflow = -1`
      — `base` is the origin alloca's mirror address (one byte
      ABOVE pointer), not `lowfat_base(0x…46f)` (which would
      resolve to `0x…460`, the previous slot). The pass's
      `calcBasePtr` traces the GEP chain `(origin + reach_index)[i]`
      → `origin` and emits the check against `lowfat_base(origin)`,
      not `lowfat_base(displaced_ptr)`. IR for the analogous
      stack-overflow ESCAPE_CALL site confirms: the size+base
      load-and-multiply chain feeds off `ptrtoint(origin_mirror)`,
      not off `ptrtoint(displaced_ptr)`.

   2. **The Unit-7 access check WAS catching some of these
      sub-cases all along.** Unit 13 didn't fail to detect the read
      sub-cases because of a runtime encoding blind spot — it
      detected them. It failed at the *MSET TYPE classification*
      because each of the 18 types has additional sub-cases that
      ONLY escape the OOB pointer (e.g., printf("%p", q), pass to
      noinline `_use`, store into a sink slot) without dereferencing
      it. With no escape instrumentation, those escape-only
      sub-cases didn't trap, and the per-type classifier wouldn't
      flip the type to DETECTED.

   3. **Unit 15 closes the escape-only sub-cases.** Adding the
      five escape-site checks makes the previously-silent
      sub-cases trap with `operation = escape (call|store)`, and
      MSET's classifier now sees uniform trapping across all
      sub-cases. The type flips from UNDETECTED → DETECTED. The
      access-side check's contribution doesn't disappear — it just
      finally gets credited at the type level.

   ### Honest residual blind spot

   The Unit 11 "lowfat_base on a same-class displaced pointer
   resolves to the neighbour's base, the unsigned `diff >=u size`
   check sees an in-bounds pointer" blind spot is real. It requires
   ALL THREE:
   (i) the displaced pointer reaches the access through a path
       calcBasePtr CANNOT trace to the origin (typically: opaque
       function arg with no GEP from a visible alloca/malloc in the
       same function, where `getInputPtrBounds` defaults to `[0,0]`
       and the check uses runtime `lowfat_base(arg)`);
   (ii) the displacement is exactly a multiple of the size class
        (so the runtime-resolved base is the neighbour's slot base,
        diff = 0);
   (iii) the access within the neighbour's slot stays under
         `lowfat_size(neighbour_slot)`.

   That intersection is NOT a property of the MSET corpus. MSET's
   18 tests construct displacement inside the same function as the
   origin allocation, with compile-visible GEP chains — so
   condition (i) fails and the access-side check uses the origin's
   bounds. For the escape-only sub-cases, condition (i) still
   fails (the GEP is visible at the escape site), and the escape
   check ALSO uses the origin's bounds.

   **A bug that displaces and dereferences in one expression
   without escaping, where calcBasePtr cannot trace the
   displacement to the origin, would still be missed.** That is
   the honest residual: the encoding blind spot didn't close; the
   MSET corpus's path to it doesn't exist.

   Evidence: `flexfat/mset/flexfat_original_detected.txt` rewritten
   from 78→96 in this commit.
3. **Report wording exact match** — `operation = escape (call)`,
   `escape (return)`, `escape (store)` byte-for-byte the reference's
   format strings, as pinned by the e2e CHECK lines.



## Unit 16 — performance parity measurement

Project's closing measurement. Reference's headline numbers
(`llvm-lowfat/README.md`, §"Experiments"): SPEC2006 at `-O2`,
**full = ~64% overhead** (all checks on), **hardened non-POW2 =
~9.8%** (`-lowfat-no-check-reads -lowfat-no-check-escapes
-lowfat-no-check-fields`), **hardened POW2 = ~7.8%**
(`build.sh sizes2.cfg 32` build + same three flags). Escape
checks ON for ALL three (the hardened flag drops escape sites
THE REFERENCE chose to drop for its 9.8% figure, but Unit-15
parity means our flag set matches one-for-one). The measurement
is calibrated to be **apples-to-apples on the flag set**, not on
the benchmark.

### Benchmark proxy (SPEC2006 not available)
SPEC CPU2006 isn't licensed for this environment. The closest
substitute set up here is a **5-benchmark synthetic
micro-corpus** under `flexfat/perf/benchmarks/`:
- `heap_churn.c` — malloc/free hot path (allocator stress).
- `array_sum.c` — stack array tight loop (lowfat stack mirror).
- `linked_list.c` — heap node alloc + pointer-chasing traversal.
- `memcpy_bulk.c` — bulk memcpy hot path (Unit-5 memops wrap).
- `opaque_access.c` — runtime-opaque pointer accesses via a
  noinline `load_at/store_at` (defeats Unit-8 static elision,
  the only benchmark in the corpus that actually exercises the
  inlined runtime fast-path check in the hot loop).

**Our numbers are INDICATIVE, not directly comparable to the
reference's SPEC2006 figures.** SPEC2006 contains many more
access patterns per benchmark, including the irregular
pointer-arithmetic / struct-field / function-pointer dispatch
patterns that the reference's 64% headline figure reflects. A
synthetic corpus this size will systematically underestimate
SPEC-scale overhead. Saying so up front rather than burying it.

### Methodology
- Host compiler `cc` (system gcc) for the uninstrumented
  baseline; in-tree `build/bin/clang` for FlexFat configs.
- Optimisation: `-O2` for all three configs (matches reference).
- N = 10 runs per (benchmark, config) cell. Configs interleaved
  per-run to spread thermal/scheduling drift. Wall-clock via
  `/usr/bin/time -f "%e"`.
- Reported statistic: **median** + **IQR** (q75 − q25).
- Hardened flag set: identical to reference's `-lowfat-no-check-
  {reads,escapes,fields}`; FlexFat names are
  `-flexfat-no-check-{reads,escapes,fields}` (Unit-10 flag surface
  parity). Plumbed via `-mllvm -flexfat-…`.

### Non-POW2 results (canonical; see `flexfat/perf/results/run_nonpow2.tsv`)

| Benchmark | uninstr (s) | full % | hardened % | hardened − full |
|---|---:|---:|---:|---:|
| heap_churn | 1.740 | **−19.5%** | −19.8% | ~0pp |
| array_sum | 2.370 | +20.9% | +20.5% | ~0pp |
| linked_list | 0.610 | +4.9% | +1.6% | −3.3pp |
| memcpy_bulk | 1.220 | +0.0% | −0.4% | ~0pp |
| opaque_access | 3.680 | +21.6% | +11.1% | **−10.5pp** |
| **arithmetic mean** | — | **+5.6%** | **+2.6%** | **−3.0pp** |

IQR ≤ 0.06 s on every cell — variance is small relative to the
signal.

### Interpretation, benchmark by benchmark

- **heap_churn went FASTER under FlexFat (−19.5%).** Matches the
  reference README's note: "optimized LowFat can even make some
  benchmarks go faster… the LowFat heap allocator happens to be
  faster than the default `malloc` for these examples." This is
  positive confirmation of the Unit-4 allocator's
  freelist+bump-pointer hot path. The hardened flag set has no
  effect here (the allocator is the bottleneck, not check
  density).
- **array_sum is +20.9% with ZERO emitted bounds checks.** The
  STATISTIC counters confirm: `NumChecks = 0`, `NumElided = 7`.
  Unit-8's lattice proves every access constant-bounded against
  the stack alloca and elides 100% of the would-be runtime
  checks. The +20.9% therefore CANNOT be runtime check cost. It
  is **lowfat-stack-mirror cache behavior**: the `int a[4096]`
  alloca lives in the lowfat-mirrored shared-memory stack region
  rather than on the native stack, which changes TLB / cache
  characteristics in the tight hot loop. The hardened flag set
  also has no effect (no checks to drop). This is a fixed-cost
  Unit-12a overhead, not a per-access check cost.
- **linked_list is +4.9% / +1.6%.** 3.3pp savings from
  `no-check-escapes` + `no-check-reads` — modest, consistent
  with the small instrumentation footprint (1 inserted check
  per function; the inner `p = p->next` is at offset 0 and Unit
  8 elides).
- **memcpy_bulk is +0.0% / −0.4% (i.e. within noise).** The
  Unit-5 `lowfat_memcpy` wrap does one bounds check per memcpy,
  amortised over 64 KB of underlying libc memcpy — invisible.
  Confirms the wrap is essentially free.
- **opaque_access is +21.6% full → +11.1% hardened (−10.5pp).**
  This is the cleanest signal in the matrix: the noinline
  `load_at`/`store_at` defeat Unit-8 elision, the inner check
  runs 1.2 B times, and the hardened `no-check-reads` drops the
  per-load check while the per-store stays. The 10.5 pp savings
  is the read-side check's per-access cost made visible.

### Reference-target gap analysis (honest)

| Config | Our mean | Reference (SPEC2006) | Gap |
|---|---:|---:|---:|
| full non-POW2 | +5.6% | ~64% | −58 pp |
| hardened non-POW2 | +2.6% | ~9.8% | −7 pp |
| hardened POW2 | **+2.2%** (Unit 17) | ~7.8% | −5.6 pp |

The full-config gap is large. Two compatible explanations, in
priority order:

1. **The corpus systematically under-stresses bounds checking.**
   Four of five benchmarks have ≤ 2 NumChecks (Unit-8 elides
   the rest); SPEC2006 has thousands per benchmark across
   irregular patterns. This is the dominant axis.
2. **Of the 5 benchmarks, only `opaque_access` lands in the
   reference's mid-range** (+21.6% full, +11.1% hardened —
   compare reference's per-benchmark scatter in
   `images/results.png`). The hardened delta on opaque_access
   (−10.5 pp) is on the same order as the reference's full→
   hardened delta (~54 pp absolute, but normalized to the
   per-benchmark base much closer to our 10 pp).

The hardened-config gap (−7 pp) is small enough that the
parity story is not failing. Per the user's acceptance shape:
"Land within striking distance of the reference targets and the
parity story closes." +2.6% vs ~9.8% is below the target; not
above. The first hypothesis the brief calls for —
**over-instrumentation** — is ruled out by the STATISTIC counter
audit below.

### Over-instrumentation audit (the `volatile`-bug lesson)

`flexfat/perf/results/stats_nonpow2.txt` captures `-stats`
output from compiling each benchmark in each instrumented
config:
- **NumChecks across all 5 benchmarks × 2 configs**: max 2,
  mean 1.2. The check density is low because Unit-8 elides
  aggressively on constant-bounded patterns.
- **NumElided**: 1–7 per cell. Healthy elision pressure.
- **NumUnknownProducers = 0 across the board.** No silent
  fallback to NONFAT (NumUnknownProducers is the canary for a
  pattern the lattice doesn't recognise; firing means a missing
  case to add, not over-instrumentation. It is silent here, as
  intended.)

No `volatile`-class regression detected. If we were missing an
emission site or the static analysis was over-eliding, we'd
expect `NumUnknownProducers > 0`; if a downstream pass were
deleting our checks, the runtime would mis-detect on the
e2e/MSET corpora (it doesn't — Unit 15 closed at 96/96 parity).
The instrumentation density is correct; the corpus just happens
to be Unit-8-friendly.

### POW2 variant — **attempted, SIGSEGV surfaced, reclassified as Unit 17**

POW2 measurement was attempted by swapping
`compiler-rt/lib/flexfat/lowfat_config.c` for the POW2 generated
copy, rebuilding the runtime, swapping the installed `lowfat.ld`
for POW2's, and re-running the matrix. Three benchmarks
(`array_sum`, `linked_list`, `opaque_access`) ran; **two
benchmarks (`heap_churn`, `memcpy_bulk`) segfaulted in
`lowfat_malloc_index`** (`gdb` confirms PC at offset `0x6c8`
inside the runtime).

**Initial framing (wrong)**: deferred runtime porting; "real porting
gap." **Correct framing (Unit 17)**: a CORRECTNESS finding, not a
perf deferral. The audit in Unit 17 establishes that every prior
POW2 "parity" claim was config-byte-diff or encoding-arithmetic only
— no test ever built+ran a POW2 binary. The root cause is more
structural than `lowfat_malloc.c` missing `#ifdef`s: the PASS's
`FlexFatSizes.inc` was single-sourced from `sizes.cfg` (non-POW2)
and committed alongside the pass; `optimizeMalloc` folded the wrong
`idx` host-side; `emitInlineBase` had no POW2 branch either. Unit 17
ports the end-to-end variant correctly with a single
`LLVM_FLEXFAT_POW2` CMake option, lands one POW2 e2e, and measures
the POW2 perf matrix (+2.2% hardened mean). See the Unit 17 section
for the audit table, root-cause sentence, and the as-landed code
changes.

### Final-unit gate (Unit 17 supersedes)
- Non-POW2 default: 86 tests, 85 passed, 1 unsupported (the POW2
  e2e). POW2 build: 82, 69 passed, 13 unsupported. 0 failed in
  either variant.
- Performance corpus committed under `flexfat/perf/`:
  benchmarks, scripts (`run_matrix.sh`, `stats_matrix.sh`,
  `analyze.py`), and **both** variant result/stats files
  (`run_nonpow2.tsv`, `run_pow2.tsv`, `stats_nonpow2.txt`,
  `stats_pow2.txt`).
- No known reference-parity deferral remains at branch close
  (Unit 17 closed the POW2 deferral that Unit 16's first draft
  documented).


## Unit 17 — POW2 end-to-end port (closes Unit 16's audit finding)

Unit 16 attempted to measure the POW2 perf matrix and instead surfaced a
SIGSEGV in `lowfat_malloc_index`. Initial framing was "deferred runtime
porting." The user pushed back: the segfault is a CORRECTNESS finding, and
the right close is either (a) finish the POW2 path, or (b) explicit scope
descope with all prior parity claims struck. **(a) was chosen.**

### Audit — what prior POW2 "parity" actually tested

| Unit | Claim in table row | What ran for POW2 | Category |
|---|---|---|---|
| 2 | "Config/table generator (byte-identical to reference, **POW2** + non-POW2)" | `flexfat/config/test/pow2-parity.test`: regenerates `lowfat_config.{c,h}` + `lowfat.ld` + `flexfat_sizes.inc` from `sizes2.cfg` via the generator, `diff -u` vs `golden/pow2/`. | **(a) static config/golden** — pure byte-diff of generator output. No runtime, no pass, no codegen. |
| 3 | "Runtime pointer-encoding core … codegen parity (**POW2 `and`**, non-POW2 `mulq`, no `div`)" | `flexfat_encoding_test.cpp::FlexFatEncoding.Pow2BaseFormula`: a gtest that uses LOCALLY-DEFINED `Pow2Magic`/`Pow2Base` math helpers and asserts `Pow2Base(p, magic) == TruthBase(p, size)`. NEVER calls `lowfat_base()` from the runtime in POW2 mode — runtime is built non-POW2-only. | **(b) encoding arithmetic only** — pure math sanity. |
| 3 | "codegen parity (POW2 `and`)" sub-claim (STATUS §"Known divergences" lines 202-208) | Side-by-side asm diff against the REFERENCE's pre-built POW2 clang binary, documenting that the reference emits `andq` for POW2. Compared the reference's output to ours; never built ours in POW2 to see what we emit. | **(b)/observational** — observed reference's binary, not FlexFat's. The pass's `emitInlineBase` had no POW2 branch (verified in source). |
| 7 | "load/store bounds-check instrumentation … inlined non-POW2 `lowfat_base`" | `load.ll` / `store.ll`: assert non-POW2 reciprocal-multiply CHECK lines. The unit row itself says "non-POW2 `lowfat_base`" — the POW2 code-emission path didn't exist. | **none for POW2.** |
| 16 | "hardened POW2 ~7.8%" listed as deferred in the gap table | Never measured. Attempt segfaulted, restored. | **none.** |

**Recorded finding:** every prior POW2 "parity" claim is (a) or (b). The
end-to-end loop — build a binary with `-fsanitize=flexfat`, link the POW2
runtime, run it — was never closed. The gap was invisible because **no test
built+ran a POW2 binary**.

### Root-cause sentence (one)

When `optimizeMalloc` (FlexFat.cpp ~996) folds a constant-size `malloc` to
`lowfat_malloc_index(idx, K)`, it computes `idx` at compile time using the
host-side `flexfatHeapSelect` (FlexFat.cpp ~384) which `#include`s the
committed (non-POW2-only) `FlexFatSizes.inc`; under a POW2 runtime the
`LOWFAT_REGION_INFO` array is sized for 30 regions, so the non-POW2
`idx`-for-large-size (e.g. 45 for `malloc(65536)`) indexes past the array
end and the next field load (`info->freelist`, offset 0x28 from the
out-of-bounds entry) dereferences uninitialized memory. **GDB-confirmed:**
`r13 = 0x40bfe8` (LOWFAT_REGION_INFO base + 3240 bytes = entry 45);
`LOWFAT_REGION_INFO` ends at `base + 0x8b8 = base + 2232` (entry 30); the
load `mov 0x28(%r13), %rbx` faults reading past the end.

This was **structural**, not a one-line `#ifdef` in `lowfat_malloc.c`: the
single-sourced sizes table baked the non-POW2 schedule into the pass at
LLVM build time, and the runtime's `lowfat_config.c` was a committed
nonpow2 copy. Both sides had to gain a build-time variant selector.

### Decision (recorded)

**Option (a): port POW2 end-to-end.** Chosen by the user when presented
with the binary choice of (a) fix the port or (b) descope POW2 and strike
the parity claims everywhere. (b) would have contradicted CLAUDE.md's
"support both variants" mandate and was not the right close.

### What landed (this unit)

**Build-time variant selector** — single `LLVM_FLEXFAT_POW2` CMake option
(default OFF), threaded through both the LLVM pass and the compiler-rt
runtime, sized-table-and-runtime-config kept in lockstep:

- `llvm/lib/Transforms/Instrumentation/CMakeLists.txt` — passes
  `-DFLEXFAT_IS_POW2=1` to `FlexFat.cpp` when the option is ON.
- Pass-side sizes table — `FlexFatSizes.inc` removed in favor of
  `FlexFatSizes_{nonpow2,pow2}.inc` (both committed alongside the pass);
  `FlexFat.cpp` `#if FLEXFAT_IS_POW2 / #include` dispatches.
- Pass-side `emitInlineBase` — single `and` for POW2, reciprocal multiply
  for non-POW2 (`#if FLEXFAT_IS_POW2` branch around the half that emits
  the magic/size table load and 128-bit `mul`).
- `compiler-rt/lib/flexfat/CMakeLists.txt` — `configure_file`s the
  variant's `lowfat_config.{c,h}` into the build dir at config time;
  prepends the build dir to the runtime's include path. `lowfat.c` switched
  to `#include <lowfat_config.c>` (angle-bracket) so the build-dir copy
  wins over the (still-committed-nonpow2) source-dir copy. `lowfat.ld`
  installation now points at the variant directory.
- Gtest CMake — passes `-DFLEXFAT_IS_POW2=…` to the gtest build; encoding
  tests gate non-POW2-only cases (`RuntimeTablesAndIndexZero`,
  `PtrInfoWorkedExample`) and malloc tests gate `EverySizeClassRoundTrips`
  / `MallocIndex` with `#if !FLEXFAT_IS_POW2`.

**Lit feature** — `flexfat-pow2` / `flexfat-nonpow2`, threaded through
both the LLVM IR test surface (`llvm/test/lit.site.cfg.py.in` +
`Instrumentation/FlexFat/X86/lit.local.cfg`) and the compiler-rt e2e
surface (`compiler-rt/test/flexfat/lit.site.cfg.py.in` +
`lit.cfg.py`). Non-POW2-only tests carry `REQUIRES: flexfat-nonpow2`
(3 IR tests baking non-POW2 CHECK lines, 10 e2e tests baking non-POW2
class sizes / region counts / thread-stack assumptions).

**Sizes-sync drift guard** — `flexfat/config/test/sizes-sync.test`
extended to check BOTH committed pass tables (`FlexFatSizes_nonpow2.inc`
and `FlexFatSizes_pow2.inc`) against BOTH goldens, and BOTH against the
corresponding `lowfat_sizes[]` array in their golden `lowfat_config.c`.
Variant-agnostic — runs in any build configuration.

**One end-to-end POW2 e2e** —
`compiler-rt/test/flexfat/TestCases/pow2_heap_boundary.c` (`REQUIRES:
flexfat-pow2`). `malloc(63)` lands in POW2 class 64 (idx 3, no class
bump-up); `p[63]` succeeds; `p[64]` traps with `operation = write`,
`size = 64`. End-to-end: pass POW2 codegen → POW2 runtime allocator →
POW2 region layout → reference-byte-identical OOB report. **First test
ever to build and run a POW2 binary.**

### Gate (variant-aware)

- **Non-POW2 (`LLVM_FLEXFAT_POW2=OFF`, default)**: 86 tests, 85 passed,
  1 unsupported (the POW2 e2e). The 85 prior tests all still pass.
- **POW2 (`LLVM_FLEXFAT_POW2=ON`)**: 82 tests visible to lit (4 gtest
  cases are `#if !FLEXFAT_IS_POW2`-compiled out, not lit-skipped),
  69 passed (the POW2 e2e + 68 variant-agnostic), 13 unsupported (the
  `REQUIRES: flexfat-nonpow2` set). **0 failed.**

### POW2 perf matrix (closes Unit 16's deferred row)

Same harness, N=10, same hardware as Unit 16's non-POW2 run. Canonical
file: `flexfat/perf/results/run_pow2.tsv`.

| Benchmark | uninstr (s) | full % | hardened % | hardened − full |
|---|---:|---:|---:|---:|
| heap_churn | 1.645 | **−20.4%** | −19.8% | ~0pp |
| array_sum | 2.270 | +20.3% | +20.7% | ~0pp |
| linked_list | 0.610 | +2.5% | +0.0% | −2.5pp |
| memcpy_bulk | 1.165 | −0.4% | −0.9% | ~0pp |
| opaque_access | 3.515 | +21.2% | +10.8% | **−10.4pp** |
| **arithmetic mean** | — | **+4.6%** | **+2.2%** | **−2.4pp** |

Reference's SPEC2006 numbers: full ~64% (no per-variant split published);
hardened non-POW2 ~9.8%; **hardened POW2 ~7.8%**. Our hardened POW2 +2.2%
is below the target by ~5.6 pp — same gap shape as non-POW2 (+2.6% vs
~9.8%, gap ~7 pp). Both gaps are dominated by corpus coverage rather than
codegen variant. **POW2 saves ~0.4 pp hardened mean and ~1 pp full mean**
vs non-POW2, consistent with the reference's ~2 pp delta. STATISTIC
counters under POW2 are identical to non-POW2 by design (the sizes table
shape doesn't change check density on this corpus).

### Audit-table revisions to prior Unit rows (this commit)

- Unit 2 row stays "byte-identical to reference, POW2 + non-POW2" — true
  at the (a) config/golden layer, which is what the test surface actually
  measures.
- Unit 3 row: "codegen parity (POW2 `and`)" was misleading pre-Unit-17.
  As of Unit 17, the pass emits POW2 `and` when built `LLVM_FLEXFAT_POW2=ON`,
  so the row is now end-to-end-correct. The STATUS prose at lines ~202-208
  was an OBSERVATION about the reference's binary; pinned forward to
  Unit 17 below.
- Unit 7 row: "inlined non-POW2 `lowfat_base`" stays accurate as the
  default-build description; `emitInlineBase` now has a POW2 branch
  selected at `LLVM_FLEXFAT_POW2=ON`.
- Unit 16 row: the "POW2 NOT measured — runtime port incomplete" caveat
  is **closed by this unit**. The hardened POW2 row in the perf gap table
  is no longer "(deferred)"; it is +2.2% measured.

The mandated "support both variants" of CLAUDE.md is now end-to-end true.
The "config+encoding parity only" framing implicit in the prior table rows
no longer applies.


## Unit 17 follow-up — three-axis variant-skew audit

Post-Unit-17 audit of the variant plumbing against three failure axes
raised after the initial port. Recorded here as the closing fix on the
cross-variant-desync surface.

### (1) Does `sizes-sync.test` actually run per-variant?

Yes, **on the COMMITTED artifacts**. The test now performs four byte-diffs:
- `golden/nonpow2/flexfat_sizes.inc` == `FlexFatSizes_nonpow2.inc` (pass)
- `golden/pow2/flexfat_sizes.inc`    == `FlexFatSizes_pow2.inc`    (pass)
- `FlexFatSizes_nonpow2.inc` values  == `golden/nonpow2/lowfat_config.c`'s `lowfat_sizes[]` (runtime)
- `FlexFatSizes_pow2.inc`    values  == `golden/pow2/lowfat_config.c`'s `lowfat_sizes[]` (runtime)

So both variants are guarded against drift across pass + runtime + generator
in any build configuration. The test does NOT inspect the BUILT artifact
(it can't — lit doesn't see compile flags); that surface is handled by (3).

### (2) Is `pow2_heap_boundary.c` a folded-index probe?

Yes. Verified by reading the emitted IR under `LLVM_FLEXFAT_POW2=ON`:
```
%0 = tail call ptr @lowfat_malloc_index(i64 3, i64 63)
```
`malloc(63)` is a constant, `optimizeMalloc` folds it host-side via
`flexfatHeapSelect(63)`, which for POW2 returns idx 3 (class 64). If the
pass were stuck on the non-POW2 sizes table, `flexfatHeapSelect(63)` would
return 4 — and under the POW2 runtime, idx 4 maps to size class 128. The
test's `size = 64` CHECK line would fail to match (the report would show
128), AND `p[64]` would silently fall within the 128-byte class and not
trap. So this test pins three things at once: pass folds the correct idx
host-side; runtime services that idx in the right class; the trap fires
at the exact one-byte boundary. **It is the malloc_class.c equivalent for
POW2.**

### (3) Could a stale build serve a mismatched pair?

Yes — and the single CMake option alone does not prevent this. CMake
re-runs `configure_file` when the option changes, and ninja rebuilds the
pass with new `-DFLEXFAT_IS_POW2=…` flags, but a hand-edited
CMakeCache, a dirty build dir from a half-completed reconfigure, or a
hand-swapped runtime archive (which is exactly how Unit 16's first
attempt was done) can produce a mismatched binary that's not caught
structurally.

**Fix landed (defensive, link-time-loud)**: the FlexFat pass emits an
extern reference to one of two variant-tagged symbols
(`__flexfat_variant_pow2` / `__flexfat_variant_nonpow2`, chosen by the
pass's compile-time `FLEXFAT_IS_POW2`), held alive by a private
`__flexfat_variant_keepalive` constant pointer that survives
dead-stripping via `llvm.used`. The runtime
(`compiler-rt/lib/flexfat/lowfat.c`) defines exactly ONE of the two
symbols, chosen by the runtime's own `LOWFAT_IS_POW2` (from the
variant-selected `lowfat_config.c`).

Cross-link demonstrations (both verified post-fix):
- Consistent build (same variant pass + runtime): links cleanly.
- **Mismatched build, non-POW2 .o + POW2 runtime archive:**
  ```
  /usr/bin/ld: hc.o:(lowfat_section_const_16+0x0):
    undefined reference to `__flexfat_variant_nonpow2'
  collect2: error: ld returned 1 exit status
  ```
- **Mismatched build, POW2 .o + non-POW2 runtime archive:**
  ```
  /usr/bin/ld: hc_pow2.o:(.data.rel.ro..L__flexfat_variant_keepalive+0x0):
    undefined reference to `__flexfat_variant_pow2'
  collect2: error: ld returned 1 exit status
  ```

The skew dies LOUDLY at link time, named, before the binary ever runs.
No silent corrupt-quietly path remains on this axis.

**Pinned by IR tests**: `variant_marker_nonpow2.ll`
(`REQUIRES: flexfat-nonpow2`) and `variant_marker_pow2.ll`
(`REQUIRES: flexfat-pow2`) assert the pass emits the right variant
symbol plus the keepalive + `llvm.used` pinning, and that the OTHER
variant symbol does NOT appear. So the link-time guard itself is now a
gate line, not just a runtime-execution observation.

**`isInterestingGlobal` carve-out**: the new keepalive constant matches
the name prefix `__flexfat_variant_` and is explicitly skipped by the
Globals pass — it is pass-internal scaffolding, not a user object to
section into a lowfat region.

### Gate post-follow-up
- Non-POW2 default: **88 tests, 86 passed, 2 unsupported** (the POW2 e2e
  + the POW2 marker IR test).
- POW2 (`LLVM_FLEXFAT_POW2=ON`): **84 tests, 70 passed, 14 unsupported**
  (the 13 `REQUIRES: flexfat-nonpow2` set + the non-POW2 marker IR test).
- **0 failed in either variant.**
