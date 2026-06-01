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
