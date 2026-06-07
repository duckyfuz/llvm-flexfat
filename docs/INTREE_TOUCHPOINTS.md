# FlexFat — in-tree touchpoints

Every in-tree file FlexFat creates or modifies, per unit. Keep this current: it
is the map of our footprint on the LLVM tree.

Legend: **+** created, **~** modified.

## Unit 1 — scaffolding (no-op pass, stub runtime, config skeleton, test harnesses)

### LLVM pass (`-passes=flexfat`, no-op)
- **+** `llvm/include/llvm/Transforms/Instrumentation/FlexFat.h` — `FlexFatPass` declaration (modeled on `AddressSanitizer.h`).
- **+** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp` — no-op `run()` returning `PreservedAnalyses::all()`.
- **~** `llvm/lib/Transforms/Instrumentation/CMakeLists.txt` — add `FlexFat.cpp` to `LLVMInstrumentation`.
- **~** `llvm/lib/Passes/PassRegistry.def` — add `MODULE_PASS("flexfat", FlexFatPass())`.
- **~** `llvm/lib/Passes/PassBuilder.cpp` — add `#include ".../Instrumentation/FlexFat.h"`.

### compiler-rt runtime (`clang_rt.flexfat`, stub)
- **+** `compiler-rt/lib/flexfat/lowfat.h` — stub header (presence sentinel).
- **+** `compiler-rt/lib/flexfat/lowfat.c` — no-op stub TU.
- **+** `compiler-rt/lib/flexfat/CMakeLists.txt` — `add_compiler_rt_component(flexfat)` + static `clang_rt.flexfat` (x86_64, `-mcmodel=large -mbmi -mbmi2 -mlzcnt`).
- **~** `compiler-rt/cmake/Modules/AllSupportedArchDefs.cmake` — `ALL_FLEXFAT_SUPPORTED_ARCH = ${X86_64}`.
- **~** `compiler-rt/cmake/config-ix.cmake` — `flexfat` in `ALL_SANITIZERS`; `filter_available_targets(FLEXFAT_SUPPORTED_ARCH …)`; `COMPILER_RT_HAS_FLEXFAT`.

### Runtime gtest (surface 2)
- **+** `compiler-rt/lib/flexfat/tests/CMakeLists.txt` — `FlexFatUnitTests` target.
- **+** `compiler-rt/lib/flexfat/tests/flexfat_test.cpp` — `TEST(FlexFat, Sentinel)`.
- **+** `compiler-rt/lib/flexfat/tests/flexfat_test_main.cpp` — gtest main.

### End-to-end + the aggregate `check-flexfat` (surface 3)
- **+** `compiler-rt/test/flexfat/CMakeLists.txt` — defines the single `check-flexfat` spanning all four surfaces.
- **+** `compiler-rt/test/flexfat/lit.cfg.py` — x86_64 gate; `%clang`, `%clang_flexfat` substitutions.
- **+** `compiler-rt/test/flexfat/lit.site.cfg.py.in` — site config template.
- **+** `compiler-rt/test/flexfat/Unit/lit.site.cfg.py.in` — gtest (Unit) site config.
- **+** `compiler-rt/test/flexfat/TestCases/sentinel.c` — e2e sentinel, `XFAIL` until the `-fsanitize=flexfat` driver flag (Unit 6).

### IR pass test (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/lit.local.cfg` — gate to X86 target.
- **+** `llvm/test/Instrumentation/FlexFat/X86/noop.ll` — `opt -passes=flexfat` leaves IR unchanged.

### Config skeleton + golden-diff harness (surface 4)
- **+** `flexfat/config/README.md` — generator/golden plan (no logic yet).
- **+** `flexfat/config/test/lit.cfg.py` — self-contained golden-diff lit config.
- **+** `flexfat/config/test/golden-diff.test` — trivially-green sentinel diff.
- **+** `flexfat/config/test/sentinel.golden` — placeholder golden.
- **+** `flexfat/config/test/.gitignore` — ignore lit `Output/`.

### Docs
- **+** `docs/LLVM_NOTES.md` — toolchain / build-config / ABI-parity record.
- **+** `docs/INTREE_TOUCHPOINTS.md` — this file.

## Unit 2 — config/table generator (byte-identical to reference, both variants)

No new LLVM-tree touchpoints; entirely self-contained under `flexfat/config/`.

- **+** `flexfat/config/lowfat-config.c` — generator, faithful port of reference `config/lowfat-config.c`.
- **+** `flexfat/config/sizes.cfg`, `sizes2.cfg` — non-POW2 / POW2 size-class inputs.
- **+** `flexfat/config/lowfat.errs` — non-POW2 precision-error cache.
- **+** `flexfat/config/golden/pow2/`, `golden/nonpow2/` — committed reference `lowfat_config.{c,h}` + `lowfat.ld` per variant.
- **+** `flexfat/config/test/pow2-parity.test`, `nonpow2-parity.test` — regenerate + byte-diff vs golden.
- **~** `flexfat/config/test/lit.cfg.py` — add `%cc` substitution.
- **~** `flexfat/config/README.md` — document the generator + layout.
- **−** `flexfat/config/test/golden-diff.test`, `sentinel.golden` — Unit-1 placeholders, replaced by the real parity tests.

## Unit 3 — runtime pointer-encoding core + init

No new LLVM-tree touchpoints; all within `compiler-rt/lib/flexfat/`.

- **~** `compiler-rt/lib/flexfat/lowfat.h` — replace the Unit-1 stub with the reference ABI header (inline `lowfat_index/size/magic/objidx/base/buffer_size`).
- **~** `compiler-rt/lib/flexfat/lowfat.c` — replace the stub with the encoding core: SIZES/MAGICS tables @ `0x200000`/`0x300000` (full index range, mprotect read-only), region reservation (PROT_NONE/MAP_NORESERVE), pointer classification, constructor(10102) + `.preinit_array`.
- **+** `compiler-rt/lib/flexfat/lowfat_config.h`, `lowfat_config.c` — the generated **non-POW2** config (the default variant; matches build.sh + SPEC §1.4), from `flexfat/config`.
- **~** `compiler-rt/lib/flexfat/CMakeLists.txt` — build `RTFlexfat` as an object library (so tests can link it); `-I` for `<lowfat_config.h>`.
- **+** `compiler-rt/lib/flexfat/tests/flexfat_encoding_test.cpp` — encoding gtests (both variants, index-0, §1.4 ptr-info golden, read-only death test).
- **~** `compiler-rt/lib/flexfat/tests/CMakeLists.txt` — link the runtime objects so the constructor initialises the tables.

## Unit 4 — heap allocator

No new LLVM-tree touchpoints; all within `compiler-rt/lib/flexfat/`.

- **+** `compiler-rt/lib/flexfat/lowfat_malloc.c` — per-size-class bump+freelist allocator (faithful port of reference `lowfat_malloc.c`), `#included` into `lowfat.c`.
- **~** `compiler-rt/lib/flexfat/lowfat.c` — add the helpers the allocator needs (per-region pthread mutex, `lowfat_rand` via getrandom, `lowfat_dont_need` via madvise, variadic `lowfat_error`/`lowfat_warning`/`lowfat_oob_error`); call `lowfat_malloc_init` from the constructor.
- **~** `compiler-rt/lib/flexfat/CMakeLists.txt` — add `RTFlexfat_noreplace` object library (`-DLOWFAT_NO_REPLACE_STD_MALLOC/_FREE`) for tests, so the unit-test process keeps libc malloc and calls `lowfat_malloc` directly.
- **+** `compiler-rt/lib/flexfat/tests/flexfat_malloc_test.cpp` — allocator gtests (every class, freelist LIFO, big-object de-page death test, realloc-no-copy, alignment family, calloc/strdup, libc fallback, free-non-heap error).
- **~** `compiler-rt/lib/flexfat/tests/CMakeLists.txt` — add the malloc test; link `RTFlexfat_noreplace` + `-ldl`.

### Allocator bookkeeping under ASan/UBSan (logic-only)
- **+** `compiler-rt/lib/flexfat/lowfat_malloc_internal.h` — shared page macros + `lowfat_freelist_s` (extracted from `lowfat_malloc.c`, single source of truth).
- **~** `compiler-rt/lib/flexfat/lowfat_malloc.c` — include the internal header instead of defining the macros/struct inline.
- **+** `compiler-rt/lib/flexfat/tests/logic/malloc_bookkeeping.cpp` — page-macro / freelist / alignment tests on ordinary memory under host ASan+UBSan.
- **+** `compiler-rt/lib/flexfat/tests/logic/lit.cfg.py` — self-contained lit config (host clang++ `-fsanitize=address,undefined`).
- **~** `compiler-rt/test/flexfat/CMakeLists.txt` — add the `logic/` suite to `check-flexfat`.

## Unit 5 — memops, classifiers, OOB reporter

No new LLVM-tree touchpoints; all within `compiler-rt/`.

- **+** `compiler-rt/lib/flexfat/lowfat_memops.c` — bounds-checked memset/memmove/memcpy (verbatim port), `#included` into `lowfat.c`.
- **~** `compiler-rt/lib/flexfat/lowfat.c` — replace the Unit-4 minimal error path with the verbatim reporter (banner, color, backtrace, `lowfat_message`, `lowfat_error/warning`, `lowfat_kind`, `lowfat_error_kind`, `lowfat_oob_error/warning/check`); switch the classifiers to the reference form; include memops.
- **+** `compiler-rt/lib/flexfat/tests/flexfat_classify_test.cpp` — classifier gtests (heap/stack/global/nonfat/index-0 + kind precedence).
- **~** `compiler-rt/lib/flexfat/tests/CMakeLists.txt` — add the classifier test.
- **+** `compiler-rt/test/flexfat/TestCases/oob_report.c` — e2e exact error-text CHECK (fixed addresses → deterministic).
- **~** `compiler-rt/test/flexfat/lit.cfg.py` — add `%clang_flexfat_runtime` (links the runtime source until the Unit-6 driver flag).
- **~** `compiler-rt/test/flexfat/lit.site.cfg.py.in` — pass `flexfat_src_dir`.

## Unit 6 — clang driver wiring (`-fsanitize=flexfat`)

First unit to touch the **clang** tree. Makes `-fsanitize=flexfat` a real flag:
recognized, x86_64-gated, code-model/feature-forced, pass-scheduled, and
runtime-linked. Pass body stays a no-op (instrumentation lands later).

### Sanitizer registration + driver args
- **~** `clang/include/clang/Basic/Sanitizers.def` — `SANITIZER("flexfat", FlexFat)`.
- **~** `clang/lib/Driver/SanitizerArgs.cpp` — `lowfat` deprecated alias → `flexfat` (in `parseArgValues`, via `warn_drv_deprecated_arg`); in `addArgs`, when flexfat is enabled, force `-mcmodel=large` + `-target-feature +lzcnt/+bmi/+bmi2`. (`-fsanitize=flexfat` itself reaches cc1 automatically via `toString(Sanitizers)`, which is what enables the pass in BackendUtil.)
- **~** `clang/include/clang/Driver/SanitizerArgs.h` — `needsFlexfatRt()` accessor.

### x86_64 toolchain gate
- **~** `clang/lib/Driver/ToolChains/Linux.cpp` — `Linux::getSupportedSanitizers` adds `FlexFat` only when `IsX86_64`; elsewhere the driver emits `unsupported option '-fsanitize=flexfat' for target '…'`.

### NPM pass scheduling (reproduce EP_ScalarOptimizerLate + EP_EnabledOnOptLevel0)
- **~** `llvm/include/llvm/Transforms/Instrumentation/FlexFat.h`, `llvm/lib/Transforms/Instrumentation/FlexFat.cpp` — convert the no-op pass **module → function** so it can sit at the function-level ScalarOptimizerLate extension point (body unchanged: still `PreservedAnalyses::all()`).
- **~** `llvm/lib/Passes/PassRegistry.def` — move `flexfat` from `MODULE_PASS` to `FUNCTION_PASS` (`opt -passes=flexfat` still works via top-level function-pass adaptation).
- **~** `clang/lib/CodeGen/BackendUtil.cpp` — `#include FlexFat.h`; `addFlexFat()` registers the pass via `registerScalarOptimizerLateEPCallback` (this tree fires it at both -O0 and -O1+), called next to `addSanitizers`.

### Runtime linking
- **~** `clang/lib/Driver/ToolChains/CommonArgs.cpp` — `collectSanitizerRuntimes` pushes `flexfat` to `StaticRuntimes` (whole-archived, pulling in the malloc interposers + `.preinit_array` constructor); its libc deps come from the existing `linkSanitizerRuntimeDeps`.

### Tests
- **+** `clang/test/Driver/fsanitize-flexfat.c` — forwarding to cc1, forced `-mcmodel=large` + lzcnt/bmi/bmi2, x86_64 gate, runtime link line, deprecated `lowfat` alias.
- **+** `clang/test/Driver/fsanitize-lowfat-deprecated.c` — the `lowfat` alias emits a stable non-fatal deprecation warning, a real `-c` compile still succeeds (exit 0, no `error:`), and it lowers to the flexfat cc1 configuration. (MSET-safety rationale recorded in `docs/STATUS.md`.)
- **+** `clang/test/CodeGen/flexfat-pass-order.c` — `-fdebug-pass-manager` proves FlexFat runs after `SROAPass` and after the CGSCC `InlinerPass` at -O2, and still runs at -O0.
- **~** `compiler-rt/test/flexfat/lit.cfg.py` — re-point `%clang_flexfat_runtime` to the real `-fsanitize=flexfat` path (+`-I` so direct `lowfat_oob_error` callers see `<lowfat.h>`).
- **~** `compiler-rt/test/flexfat/TestCases/sentinel.c` — drop `XFAIL`; now a real end-to-end pass through the flag.
- **~** `compiler-rt/test/flexfat/TestCases/oob_report.c` — comment-only; now exercises the flag instead of the direct-compile workaround.

## Unit 7 — load/store bounds-check instrumentation

First unit where the pass does real work (the LOAD/STORE path). No new
build/driver touchpoints; entirely within the existing pass + test surfaces.

### Pass
- **~** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp` — replace the no-op body with the LOAD/STORE instrumentation: `getInterestingInsts` (plan a check per load/store, skip `nosanitize`), `calcBasePtr` (recurse GEP/bitcast/addrspacecast/select/PHI; an allocation is its own base; alloca/global/constant → non-fat for now; argument/load/inttoptr/extract → inline base), the inlined **non-POW2** `lowfat_base` (reciprocal multiply, no div), and the inlined `lowfat_oob_check` (`idx = base>>35`, `_LOWFAT_SIZES` load @ `0x200000`, `diff = ptr-base`, unsigned `diff >=u size` compare, weighted branch to a cold `lowfat_oob_error` block + `unreachable`). Uses `TargetLibraryAnalysis`, `MemoryBuiltins` (`isAllocationFn`), and `SplitBlockAndInsertIfThen`. (`LLVMInstrumentation` already links `Analysis`/`TransformUtils`.)

### IR tests (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/load.ll` — single load through a fat pointer argument: the inlined base + check sequence + `2000000000:1` branch weights.
- **+** `llvm/test/Instrumentation/FlexFat/X86/store.ll` — single store through a GEP: base taken from the GEP's pointer operand; `info = WRITE = 1`.
- **+** `llvm/test/Instrumentation/FlexFat/X86/nonfat.ll` — alloca/global accesses are left uninstrumented (NULL base ⇒ non-fat ⇒ check dropped).
- **−** `llvm/test/Instrumentation/FlexFat/X86/noop.ll` — the Unit-1 "pass is a no-op on a load/store" sentinel; obsolete now that load/store are instrumented (superseded by `nonfat.ll`).

### Codegen-placement test (surface 1, asm level)
- **+** `clang/test/CodeGen/flexfat-error-block-placement.c` — asserts at the x86_64 asm level that the cold `lowfat_oob_error` block is emitted out of line (after the fast-path `ret`), guarding the intentionally-inverted branch-weight direction against a future `MachineBlockPlacement` change.

### e2e tests (surface 3)
- **+** `compiler-rt/test/flexfat/TestCases/heap_oob.c` — the SPEC §1.4 / README heap example (`noinline get()`) must trap with the deterministic report fields (`operation=read`, `size=16`, `overflow=+84`, `(heap)`); addresses are ASLR-random and regex-matched.
- **+** `compiler-rt/test/flexfat/TestCases/in_bounds.c` — an in-bounds program exits 0 (no false positive).

## Unit 8 — static bounds analysis (provably-safe check elision)

No new build/driver touchpoints; entirely within the pass + IR test surface.

### Pass
- **~** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp` — add the `Bounds` lattice (lb=0, ub; `NONFAT`=INT64_MAX, `UNKNOWN`=INT64_MIN) and `getPtrBounds`/`getConstantPtrBounds`/`getInputPtrBounds` (port of LowFat.cpp:61-137, :426-621). `run()` now consults `getPtrBounds(Ptr).isInBounds(0)` and skips a check entirely when the access is provably in-bounds (the `addToPlan` gate), before `calcBasePtr`. Adds the `-flexfat-no-check-fields` flag (opaque-pointer analog of `-lowfat-no-check-fields`, applied at the GEP using the source element type), an internal `-flexfat-no-elide` flag (A/B measurement), and `NumChecks`/`NumElided` statistics. The unrecognized-producer fallback (`getPtrBounds` `else`) keeps the reference's `NONFAT` elide but emits a real `FlexFatDiag` `(BUG) unknown pointer type` warning (port of `LowFatWarning`) and bumps `NumUnknownProducers`, so an incomplete recognition list surfaces as a test failure, not a silent unsound elision.

### IR tests (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/bounds.ll` — positives (constant in-bounds GEP off malloc/alloca/global, select/PHI merge within the min → check elided) and negatives (constant OOB off malloc, dynamic GEP, unknown-provenance loaded pointer → check kept). malloc carries clang's `allockind`/`allocsize(0)` attributes so `getObjectSize` recovers the size.
- **+** `llvm/test/Instrumentation/FlexFat/X86/no_check_fields.ll` — `-flexfat-no-check-fields` flips a constant field GEP off an input pointer from checked (default) to elided.
- **~** `llvm/test/Instrumentation/FlexFat/X86/load.ll` — a *direct* input-pointer deref is now elided by the analysis, so the canonical "checked load" test moves to a dynamic GEP (still checked).
- **+** `llvm/test/Instrumentation/FlexFat/X86/unknown_producer.ll` — finding-3 tradeoff: untraceable pointers (argument / opaque-call result / inttoptr) dereferenced at offset 0 are elided, a positive offset off them is checked; also a corpus canary (`--implicit-check-not="unknown pointer"`).
- **+** `llvm/test/Instrumentation/FlexFat/X86/unknown_producer_diag.ll` — negative control: an `atomicrmw`-derived pointer trips the fallback, asserting the `(BUG) unknown pointer type` warning fires and the check is still elided.
- **~** `llvm/test/Instrumentation/FlexFat/X86/bounds.ll` — RUN extended with `2>&1 ... --implicit-check-not="unknown pointer"` (corpus canary over its malloc/gep/alloca/global/select/PHI/load forms).
- **+** `compiler-rt/test/flexfat/TestCases/no_unknown_producers.c` — e2e corpus canary: a producer-diverse program compiled through `-fsanitize=flexfat -O2` must emit no fallback warning (`NumUnknownProducers == 0`).

## Unit 9 — intrinsic checks, replaceUnsafeLibFuncs, optimizeMalloc

First unit where pass-emitted symbols (`lowfat_mem*`, `lowfat_malloc_index`) must
resolve against the Units 3–5 runtime — the e2e are the real integration test.

### Pass
- **~** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp`:
  - **mem-intrinsic checks** (`instrumentMemIntrinsic`, port of LowFat.cpp:913-947): `llvm.memcpy`/`memmove` validate `Src+len` and `Dst+len`; `llvm.memset` validates `Dst+len`; info codes `MEMCPY`=2 / `MEMSET`=3. Reuses the Unit 7/8 elide+`insertBoundsCheck` path.
  - **replaceUnsafeLibFuncs** (`replaceLibFunc`, port of LowFat.cpp:1071-1118): redirects calls to `memcpy`/`memset`/`memmove` (always) and the allocator family `malloc`/`free`/`calloc`/`realloc`/`posix_memalign`/`aligned_alloc`/`valloc`/`memalign`/`pvalloc`/`strdup`/`strndup` + C++ `new`/`delete` (unless `-flexfat-no-replace-malloc`) to their `lowfat_*` equivalents. Per call site (a function pass must not RAUW module declarations).
  - **optimizeMalloc** (`optimizeMalloc`, port of LowFat.cpp:330-384): a constant `lowfat_malloc(K)` becomes `lowfat_malloc_index(idx, K)` with `idx = heap_select(K)` folded at compile time via host-side `flexfatHeapSelect` over an embedded `kLowFatSizes[]` (copied from the generated `lowfat_config.c`, must stay in sync).
  - New `-flexfat-no-replace-malloc` flag; `I8Ty` for byte GEPs.

### IR tests (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/replace_libfuncs.ll` — memcpy/memset/memmove + malloc family + `_Znwm` rewritten to `lowfat_*`; `-flexfat-no-replace-malloc` leaves the allocator family untouched (mem-intrinsics still replaced).
- **+** `llvm/test/Instrumentation/FlexFat/X86/optimize_malloc.ll` — constant `malloc(100)` → `lowfat_malloc_index(i64 7, i64 100)`; dynamic stays `lowfat_malloc`.

### e2e tests (surface 3)
- **+** `compiler-rt/test/flexfat/TestCases/memcpy_oob.c` — a memcpy overrun traps with `operation = memcpy` (`size = 16`, `overflow = +84`).
- **+** `compiler-rt/test/flexfat/TestCases/memset_oob.c` — a memset overrun traps with `operation = memset`.
- **+** `compiler-rt/test/flexfat/TestCases/mem_inbounds.c` — in-bounds memcpy/memset (+ a constant-malloc path) exit 0.

### Size-table single-sourcing + drift guards (Unit 9 hardening)
- **~** `flexfat/config/lowfat-config.c` — the generator now also emits `flexfat_sizes.inc` (the size-class table body) from the same run as `lowfat_config.c`'s `lowfat_sizes[]`.
- **+** `flexfat/config/golden/{nonpow2,pow2}/flexfat_sizes.inc` — committed golden for the new artifact.
- **+** `llvm/lib/Transforms/Instrumentation/FlexFatSizes.inc` — the pass's copy (generated, == golden nonpow2); `FlexFat.cpp`'s `kLowFatSizes[]` now `#include`s it instead of a hand-copied array.
- **~** `flexfat/config/test/{nonpow2,pow2}-parity.test` — also diff the regenerated `.inc` against golden.
- **+** `flexfat/config/test/sizes-sync.test` — byte-for-byte drift guard: pass `.inc` values == runtime `lowfat_sizes[]` values (and pass `.inc` == golden `.inc`).
- **+** `compiler-rt/test/flexfat/TestCases/malloc_class.c` — behavioral drift guard: `malloc(100)` → region 7 / class 112; `p[111]` passes, `p[112]` traps with `size = 112`.

## Unit 10 — option surface + SpecialCaseList blacklist

No new build/driver touchpoints; all within the pass + test surfaces.

### Pass
- **~** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp` — add the option surface: `-flexfat-no-check-{reads,writes,memset,memcpy,escapes}` (`filterKind`), `-flexfat-check-whole-access` (threads access_size = sizeof(*ptr)-1 through `checkAccess`/`insertBoundsCheck`), `-flexfat-no-replace-{alloca,globals}` (inert forward-decls), `-flexfat-no-check-blacklist` (`SpecialCaseList`, cached, `isBlacklisted` skips a function/module), and the error-block modes in `insertBoundsCheck` (`-flexfat-no-abort` → `lowfat_oob_warning` + branch-back; `-flexfat-signal` → inline `ud2` + unreachable; default → `lowfat_oob_error` + unreachable). New includes: `InlineAsm.h`, `SpecialCaseList.h`, `VirtualFileSystem.h`.

### IR tests (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/check_suppression.ll` — `-no-check-reads`/`-writes` drop the matching info-0/1 check, keep the other.
- **+** `llvm/test/Instrumentation/FlexFat/X86/mem_suppression.ll` — `-no-check-memcpy`/`-memset` drop the info-2/3 checks.
- **+** `llvm/test/Instrumentation/FlexFat/X86/whole_access.ll` — `-check-whole-access` emits `sub i64 %size, 3` (sizeof(i32)-1) before the compare; default does not.
- **+** `llvm/test/Instrumentation/FlexFat/X86/blacklist.ll` (+ `Inputs/flexfat_blacklist.txt`) — the listed function emits no checks; an unlisted one in the same module still does.

### e2e tests (surface 3)
- **+** `compiler-rt/test/flexfat/TestCases/error_no_abort.c` — `-flexfat-no-abort` reports `LOWFAT WARNING` and exits 0 (continues; the overrun stays on the committed page).
- **+** `compiler-rt/test/flexfat/TestCases/error_signal.c` — `-flexfat-signal` dies with SIGILL (132), no report (`sh -c` pins the exact signal).

## Unit 11 — verification harness + MSET differential

No LLVM/compiler-rt source touchpoints; the consolidated `check-flexfat` gate
(Unit 1) already aggregates all four surfaces (confirmed 46/46). New artifacts are
the differential harness and the glibc landmine validator.

### MSET differential (committed evidence)
- **+** `flexfat/mset/flexfat_original.xml` — base FlexFat MSET config (`-fsanitize=flexfat`, exit-6 keyed); analogue of the reference `lowfat_original.xml`.
- **+** `flexfat/mset/flexfat.xml` — hardened config, adds `-mllvm -flexfat-check-whole-access`; analogue of `lowfat.xml`.
- **+** `flexfat/mset/flexfat_original_detected.txt`, `flexfat/mset/flexfat_detected.txt` — FlexFat's detected-type sets (base/hardened), committed for auditability against the reference oracle.
- **+** `flexfat/mset/README.md` — how the differential is run + harvested; result pointer to STATUS.md.
  - The MSET corpus + evaluator live in the sibling `MSET/` tree (not vendored). The reference oracle is `MSET/build/lowfat_original_detected.txt` (96) / `lowfat_detected.txt` (36).

### glibc TID/JOINID landmine validator
- **+** `flexfat/config/lowfat-check-config.c` — port of the reference `config/lowfat-check-config.c`; validates `LOWFAT_TID_OFFSET`/`LOWFAT_JOINID_OFFSET` (from the committed `golden/nonpow2/lowfat_config.c`) against host glibc. Build: `cc -I golden/nonpow2 -Wno-unused-function -Wno-unused-variable -o lowfat-check-config lowfat-check-config.c -lpthread`. Result on glibc 2.39: `OK`, exit 0. Re-run on the target glibc before Part II thread support.

### Result
Full differential + per-delta classification in [STATUS.md](STATUS.md) "Unit 11".
Headline: FlexFat's detected set is a strict subset of the reference's (0 false
detections); the only genuine heap-origin misses are the 6 Heap→Heap offset-0
adjacency-blind-spot types, identical across base and hardened.

## Unit 12a — stack runtime: SHM, MAP_SHARED stack regions, pivot

Runtime-only changes; the pass is unchanged (alloca lowfatification is Unit
12b). All within `compiler-rt/lib/flexfat/` plus two new e2e + one new gtest.

### Runtime
- **~** `compiler-rt/lib/flexfat/lowfat.c`:
  - **+** `<fcntl.h>` include for `O_EXCL` / `F_SETLEASE`.
  - **~** `lowfat_map(addr, len, r, w, fd)` — extended with the `fd` parameter
    (`fd >= 0` ⇒ `MAP_SHARED`, else `MAP_PRIVATE|MAP_ANONYMOUS`). Two existing
    callers updated.
  - **+** `lowfat_create_shm(size)` — port of reference `lowfat_linux.c:80-107`
    (`/dev/shm/flexfat.XXXXXX...tmp` random-suffix, `O_CREAT|O_EXCL`, unlink,
    `F_SETLEASE`, ftruncate; returns the fd).
  - **+** `lowfat_envp` static — populated in `lowfat_preinit`; consumed once
    by the pivot and cleared.
  - **+** `lowfat_stack_alloc()` — single-thread bump allocator over
    `LOWFAT_STACKS_START`; per-slot mprotect RW in every mirror via
    `lowfat_stacks[]`. No Fisher-Yates ASLR / thread freelist yet (Part III).
  - **+** `lowfat_stack_pivot_2(stack_top)` — port of reference
    `lowfat.c:524-575`: envp-walk → `lowfat_stack_alloc` → `memcpy` →
    scan-and-patch self-referential pointers.
  - **+** `lowfat_stack_pivot` asm trampoline — verbatim port of reference
    `lowfat.c:577-586` (5-instruction `%rsp` swap).
  - **~** `lowfat_init` — after `lowfat_malloc_init`, init the stack mutex,
    create the shm fd, map each `lowfat_stacks[]` entry's stack sub-range
    `MAP_SHARED` to it, close the fd, then call `lowfat_stack_pivot()` as
    the LAST init step.
  - **~** `lowfat_preinit` — captures `envp` before calling `lowfat_init`.

### Tests
- **+** `compiler-rt/lib/flexfat/tests/flexfat_stack_test.cpp` — three gtests:
  `ShmAliasing` (fresh shm fd + two distinct VAs see the same bytes),
  `StackTableIndexing` (clzll-based class index returns expected
  size/mask/offset from the Unit-2-generated tables), `StackRegionAliasing`
  (a write through the master region's address is visible via the size-class
  mirror — implicitly confirms the constructor's MAP_SHARED setup).
- **~** `compiler-rt/lib/flexfat/tests/CMakeLists.txt` — add the new source.
- **~** `compiler-rt/lib/flexfat/tests/flexfat_test_main.cpp` — globally set
  `gtest_death_test_style = "threadsafe"` to work around the MAP_SHARED+fork
  alias (see STATUS.md "Unit 12a").
- **+** `compiler-rt/test/flexfat/TestCases/pivot_classifies_stack.c` — e2e:
  a `main` that takes `&local` and asserts `lowfat_is_ptr` + `lowfat_is_stack_ptr`
  return true. Pins "pivot runs before main."
- **+** `compiler-rt/test/flexfat/TestCases/stack_heavy_clean.c` — e2e: a
  stack-heavy program (1000-deep recursion, 8 KiB local, args/env access) at
  -O0 and -O2 must exit 0 — pivot regression-catch.

## Unit 12b — alloca lowfatification (pass half)

Pass-only changes; no runtime touchpoints (the runtime side is 12a). The pass
adds an escape-gated alloca-lowfatification pre-pass before the existing
bounds-check sweep.

### Pass
- **~** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp`:
  - **+** static `doesIntEscape` + `doesAllocaEscape` + `isInterestingAlloca`
    (ports of LowFat.cpp:810-846, :1343-1414, :1419-1430). Escape rule:
    escaping ⇒ low-fat, non-escaping ⇒ native (the reference's direction,
    not SPEC line 336's English wording).
  - **+** `kStackMirrorMD = "flexfat.stack.mirror"` metadata kind, tagged on
    the constant-offset mirror gep produced by `makeAllocaLowFatPtr`.
  - **+** `kStackSizes[]`/`kStackMasks[]`/`kStackOffsets[]` — in-pass tables
    byte-identical to the runtime's `lowfat_stack_*` arrays (idx 0..64),
    used by the fixed-alloca fast path to fold every constant at compile time.
  - **+** `kMaxStackAllocSize = 33554432` — the runtime's 32 MiB cap; allocas
    larger than this stay native.
  - **+** `FlexFat::makeAllocaLowFatPtr(AllocaInst*)` (port of LowFat.cpp:
    1512-1677). Two paths: fixed (idx, newSize, mask, offset all immediates,
    optional byte-array replacement at the class boundary) and VLA
    (`llvm.ctlz.i64`, runtime table loads for offset/size/mask,
    `and+inttoptr` align, `llvm.stackrestore`). Inline IR throughout — the
    reference's `addLowFatFuncs` helper-call path is intentionally NOT
    ported (Unit 7 architecture decision, "no fast-path call").
  - **+** `FlexFat::getStackTable(StringRef)` — declares the extern
    `lowfat_stack_{offsets,sizes,masks}` globals (read-only, exported by
    `lowfat_config.c`). Used by the VLA path only.
  - **~** `calcBasePtr` — a GEP carrying `!flexfat.stack.mirror` is treated
    as a fat pointer in its own right (`emitInlineBase` on the GEP), not
    walked back through to the non-fat alloca.
  - **~** `getPtrBounds` — same metadata: mirror gep → `Bounds::empty()` so
    a direct deref is elided (input-pointer convention) but any positive
    offset is checked.
  - **~** `-flexfat-no-replace-alloca` description updated (no longer
    "inert"); the flag is now functional.
  - **+** Phase-0 of `FlexFat::run()`: scan for `isInterestingAlloca`,
    lowfatify each. Runs BEFORE the load/store / mem-intrinsic / libcall
    phases so the bounds-check path sees mirror geps.
  - **+** Idempotence guard in `isInterestingAlloca` — skip allocas whose
    user list already contains a mirror-tagged gep (fires after inlining
    when a callee's lowfat alloca enters our function).

### IR tests (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/stack_lowfatify.ll` — fixed
  alloca whose address is stored escapes; pinned: byte-array replacement
  at the class boundary, mirror gep with the immediate offset, lifetime
  calls dropped.
- **+** `llvm/test/Instrumentation/FlexFat/X86/stack_escape_via_gep.ll` —
  indirect escape through a GEP-derived pointer still triggers lowfat;
  pins the recursive predicate.
- **+** `llvm/test/Instrumentation/FlexFat/X86/stack_nonescaping.ll` — load/
  cmp/select/self-store-only alloca stays native; no mirror, no
  replacement.
- **+** `llvm/test/Instrumentation/FlexFat/X86/stack_vla.ll` — VLA path:
  `llvm.ctlz.i64`, inline table loads from
  `@lowfat_stack_{offsets,sizes,masks}`, `and+inttoptr` align,
  `llvm.stackrestore`, mirror gep.
- **+** `llvm/test/Instrumentation/FlexFat/X86/stack_no_replace_alloca.ll`
  — `-flexfat-no-replace-alloca` suppresses the entire transform (the
  Unit-10 flag finally gets its behavioral test).

### e2e (surface 3)
- **+** `compiler-rt/test/flexfat/TestCases/stack_oob.c` — a stack buffer
  overflow on an escaping local traps with the deterministic report
  fields (`operation = write`, `pointer = … (stack)`, `size = 32` — the
  class-size bump-up).

## Unit 13 — global lowfatification (pass + driver + runtime install)

### Pass
- **+** `llvm/include/llvm/Transforms/Instrumentation/FlexFat.h` — declare
  `FlexFatGlobalsPass` (module pass) alongside the function pass.
- **~** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp`:
  - **+** `kMaxGlobalAllocSize = 67108864` (64 MiB cap).
  - **+** `isInterestingGlobal` (port of LowFat.cpp:1435-1458).
  - **+** `makeGlobalVariableLowFatPtr` (port of LowFat.cpp:1467-1505):
    skip declarations; warn-and-skip oversized; promote Common→WeakAny;
    align to class boundary; write `section "lowfat_section_<size>"` or
    `..._const_<size>` per `isConstant()`.
  - **+** `FlexFatGlobalsPass::run` — snapshot `M.globals()` then process.
  - **~** `calcBasePtr` — a `GlobalVariable` with a `lowfat_section_*`
    section is treated as fat; emit `emitInlineBase(GV)`.
  - **~** `getConstantPtrBounds` — comment update; semantics unchanged
    (returns source `TypeAllocSize` so static analysis still elides
    provably in-bounds accesses, and the dynamic check fires for the rest).
- **~** `llvm/lib/Passes/PassRegistry.def` — add
  `MODULE_PASS("flexfat-globals", FlexFatGlobalsPass())`.
- **~** `clang/lib/CodeGen/BackendUtil.cpp` — `addFlexFat` now registers
  `FlexFatGlobalsPass` at PipelineStart so it runs BEFORE the function
  pipeline (which contains FlexFatPass and depends on the section being
  set when calcBasePtr inspects globals).

### Driver
- **~** `clang/lib/Driver/ToolChains/CommonArgs.cpp` — on every flexfat
  link, push `-T <resource>/lowfat.ld` (path derived from the runtime
  archive's actual location, so it tracks per-target-runtime-dir layout)
  and `-z max-page-size=0x1000` (default 2 MiB pages × thousands of empty
  lowfat sections would otherwise blow the executable to GiB).
- **~** `clang/lib/Driver/ToolChains/Gnu.cpp` — suppress default-PIE
  when `-fsanitize=flexfat` is active and the user didn't explicitly
  ask for `-pie`. lowfat.ld pins sections to absolute addresses (e.g.
  `0xbffff7000`); PIE relocates them, which silently breaks global
  lowfatification on distros where PIE is the default (Rocky 10,
  modern Debian, etc.). User-specified `-pie` still wins.

### Runtime install
- **~** `compiler-rt/lib/flexfat/CMakeLists.txt` — copy
  `flexfat/config/golden/nonpow2/lowfat.ld` into
  `${COMPILER_RT_OUTPUT_LIBRARY_DIR}/${triple}/` (same directory as
  `libclang_rt.flexfat.a`) at build time; install it to the matching
  path at install time. Path-derivation in the driver assumes this
  exact placement.

### IR tests (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/global_lowfatify.ll` —
  mutable global → `lowfat_section_<size>` at class align; const →
  `lowfat_section_const_<size>`.
- **+** `llvm/test/Instrumentation/FlexFat/X86/global_excluded.ll` —
  thread-local, user-section, alignment-> 16, declaration-only,
  oversized (> 64 MiB) all stay untouched.
- **+** `llvm/test/Instrumentation/FlexFat/X86/global_common.ll` —
  Common-linkage promoted to WeakAny + sectioned.
- **+** `llvm/test/Instrumentation/FlexFat/X86/global_no_replace.ll` —
  `-flexfat-no-replace-globals` suppresses; the Unit-10 inert
  forward-decl finally has its behavioral test.

### e2e (surface 3)
- **+** `compiler-rt/test/flexfat/TestCases/global_classify.c` — the
  link-level classifier test: a regular `int g;` lands in `[16 GiB,
  24 GiB)` of its lowfat region, `lowfat_is_global_ptr(&g) == true`,
  `lowfat_base(&g) == &g`, `lowfat_size(&g) == 16`. Proves the linker
  half (lowfat.ld INSERT AFTER + max-page-size + no-PIE).
- **+** `compiler-rt/test/flexfat/TestCases/global_oob.c` — global
  buffer overflow traps with `pointer = … (global)`, `size = 32`
  (class bump-up from 16).
- **+** `compiler-rt/test/flexfat/TestCases/global_clean.c` — large
  mixed mutable+const globals, clean exit at -O0 and -O2.

## Unit 14a — threads + build gate

Runtime, build-system, and tests; no LLVM/clang side.

### Build gate
- **~** `flexfat/config/lowfat-check-config.c` — rewrite for build-gate
  use: expected-vs-found error messages, cond-var sync so the JOINID
  check is guaranteed to follow the worker's TID check (the reference
  version had a race that could print `OK` before the worker ran).
- **~** `compiler-rt/lib/flexfat/CMakeLists.txt` — `add_custom_command`
  builds the validator with the host C compiler, includes the configured
  `lowfat_config.c`, runs it as a build step whose success writes a
  stamp; runtime archive (`flexfat`) DEPENDS on the stamp.

### Runtime (threads)
- **~** `compiler-rt/lib/flexfat/lowfat.c`:
  - **+** `LOWFAT_STACK_BASE(ptr)` macro.
  - **+** `lowfat_stack_perm[LOWFAT_NUM_THREAD_STACKS]` (Fisher-Yates
    permutation; non-static for gtest verification).
  - **+** `struct lowfat_stack_freelist_s` + `lowfat_stack_freelist`.
  - **+** `lowfat_is_thread_dead` + `lowfat_force_thread_dead`
    (read/write TID/JOINID at the build-validated offsets).
  - **+** `lowfat_stack_free(pthread_t)` and `lowfat_force_stack_free
    (void *)` (the failure-recovery path; non-static for gtest).
  - **~** `lowfat_stack_alloc` rewritten: walks freelist first, then
    bump-allocates via `lowfat_stack_perm[freeidx]`.
  - **~** `lowfat_init`: Fisher-Yates shuffle of `lowfat_stack_perm`
    BEFORE the pivot.
  - **+** `pthread_create` interposer via `dlsym(RTLD_NEXT, ...)` +
    `pthread_attr_setstack`. Gated by `LOWFAT_NO_REPLACE_PTHREAD_CREATE`
    so the gtest no-replace variant keeps glibc's pthread_create.
- **~** `compiler-rt/lib/flexfat/CMakeLists.txt` — add
  `-DLOWFAT_NO_REPLACE_PTHREAD_CREATE` to `RTFlexfat_noreplace`.

### Tests
- **+** `compiler-rt/lib/flexfat/tests/flexfat_threads_test.cpp` —
  4 gtests: `FisherYatesIsPermutation` (every value in [0, 128) appears
  exactly once), `AllocReturnsInStackSubrange` (class-aligned + lowfat
  stack classifier), `SlotReclamationAfterForceFree` (TID-final-state
  reclamation round-trip), `ConcurrentAllocStress` (4 threads × 200
  alloc/free iterations across 6 size classes — first real concurrency
  exercise for the Unit-4 per-region malloc mutexes).
- **~** `compiler-rt/lib/flexfat/tests/CMakeLists.txt` — add the new source.
- **+** `compiler-rt/test/flexfat/TestCases/thread_classify.c` — e2e: a
  `pthread_create`d worker takes `&local` and asserts `lowfat_is_ptr` +
  `lowfat_is_stack_ptr` both true. Pins "interposer ran and gave the
  worker a lowfat slot."
- **+** `compiler-rt/test/flexfat/TestCases/thread_stack_oob.c` — e2e:
  stack OOB inside a spawned thread traps with `pointer = … (stack)`,
  `size = 32`.
- **+** `compiler-rt/test/flexfat/TestCases/threads_alloc_stress.c` —
  e2e: 4 threads × 2000 malloc/free iterations × 6 classes, -O0 and -O2.

## Unit 14b — fork interposer

Runtime + test surface; no LLVM/clang changes.

### Runtime
- **~** `compiler-rt/lib/flexfat/lowfat.c`:
  - **+** `<sched.h>`, `<setjmp.h>`, `<signal.h>`, `<sys/wait.h>` includes.
  - **+** `struct lowfat_fork_info` (PROCESS_SHARED mutex/cond, done
    flag, parent frame address, jmp_buf).
  - **+** `lowfat_fork_child_wrapper` — runs on temp stack: fresh
    `lowfat_create_shm`, `mmap MAP_SHARED|MAP_FIXED` over size-class 1's
    stack range, mprotect+memcpy parent's live stack pages, cond_signal,
    loop remap of remaining stack regions, close fd, longjmp.
  - **+** `lowfat_fork_wrapper` (LOWFAT_NOINLINE) — init PROCESS_SHARED
    cond/mutex on temp stack, clone(SIGCHLD), wait on cond, cleanup.
  - **+** `lowfat_fork()` with `LOWFAT_ALIAS("fork")` — allocate temp
    stack, setjmp env at top, dispatch to wrapper (parent path) /
    munmap+return 0 (child path arriving via longjmp).
  - Gated by `LOWFAT_NO_REPLACE_FORK` (NOT set on `RTFlexfat_noreplace`
    so the gtest binary uses the interposer).

### Tests
- **+** `compiler-rt/test/flexfat/TestCases/fork_isolation.c` — bare
  `fork()` parent/child stack-write isolation. Red against 14a runtime
  (SIGSEGV at exit 139), green under 14b (exit 0, "isolated"). Uses
  `static unsigned int *volatile sentinel_escape_holder = &sentinel;`
  to keep the volatile alloca alive under -O2 (see STATUS finding).
- **+** `compiler-rt/test/flexfat/TestCases/fork_oob.c` — fork-then-
  stack-OOB-in-child traps with `pointer = … (stack)`, `size = 32`.
- **~** `compiler-rt/lib/flexfat/tests/flexfat_test_main.cpp` — REVERT
  the 12a `gtest_death_test_style = "threadsafe"` override; fast-mode
  death tests are safe under the 14b interposer.

## Unit 15 — escape checks

Pass + tests; no runtime change (the runtime's `lowfat_error_kind`
already formatted info codes 5-9 as `escape (call/return/store/
ptr2int/insert)` — verified verbatim by the e2e CHECKs).

### Pass
- **~** `llvm/lib/Transforms/Instrumentation/FlexFat.cpp`:
  - **+** `kInfoEscape{Call,Return,Store,Ptr2Int,Insert}` = 5..9
    constants matching `lowfat.h:45-49`.
  - **+** Five granular cl::opt flags:
    `-flexfat-no-check-escape-{call,return,store,ptr2int,insert}`.
    The umbrella `-flexfat-no-check-escapes` (Unit-10 forward-decl)
    description updated — no longer inert.
  - **~** `filterKind`: each escape info code consults
    `Cl<Granular> || ClNoCheckEscapes`.
  - **+** `isUglyGEP` — verbatim port of LowFat.cpp:854-863 (metadata
    `uglygep` check).
  - **~** `FlexFat::run` Phase-1 sweep: also collects escape sites
    (store-of-ptr, ptrtoint with escaping int + non-ugly-gep source,
    call/invoke ptr args (excl. doesNotAccessMemory callees), ret-ptr,
    insertvalue/insertelement of ptr).
  - **+** Phase 4 — Escapes processed via `checkAccess(I, Ptr, Info, 0)`,
    AFTER load/store + mem-intrinsics, BEFORE libfunc replacement
    (Phase 5) so escape checks anchor at the original call sites
    before `replaceLibFunc` erases them.

### IR tests (surface 1)
- **+** `llvm/test/Instrumentation/FlexFat/X86/escape_call.ll` — info 5.
- **+** `llvm/test/Instrumentation/FlexFat/X86/escape_return.ll` — info 6.
- **+** `llvm/test/Instrumentation/FlexFat/X86/escape_store.ll` — info 7.
- **+** `llvm/test/Instrumentation/FlexFat/X86/escape_ptr2int.ll` — info 8.
- **+** `llvm/test/Instrumentation/FlexFat/X86/escape_insert.ll` — info 9
  (insertvalue + insertelement).
- **+** `llvm/test/Instrumentation/FlexFat/X86/escape_ptr2int_ugly_gep.ll`
  — falsifiable port of the ugly-GEP carve-out: ptr2int of a GEP
  tagged `!uglygep` MUST NOT emit `lowfat_oob_error(i32 8, …)`.
- **+** `llvm/test/Instrumentation/FlexFat/X86/escape_umbrella_suppress.ll`
  — `-flexfat-no-check-escapes` suppresses all 5 codes at once.

### e2e (surface 3)
- **+** `compiler-rt/test/flexfat/TestCases/escape_call_oob.c` —
  OOB pointer passed to `printf`; trap with
  `operation = escape (call)`, `size = 32` (malloc(16) class bump-up).
- **+** `compiler-rt/test/flexfat/TestCases/escape_store_oob.c` —
  OOB pointer stored to a global slot; trap with
  `operation = escape (store)`.
- **+** `compiler-rt/test/flexfat/TestCases/escape_return_oob.c` —
  noinline function returning an OOB pointer; trap with
  `operation = escape (return)`.
