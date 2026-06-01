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
