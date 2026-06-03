# FlexFat ↔ MSET differential harness

These are the FlexFat-side [MSET](https://github.com/ARM-software/mset-tool-suite)
(Memory Safety Evaluation/Test suite) sanitizer configs used by Unit 11 to run the
**differential** between FlexFat and the reference LowFat oracle. They are committed
here as the verbatim record of what was evaluated; the MSET corpus and evaluator
themselves live in the sibling `MSET/` tree (not vendored into this repo).

## Configs
- `flexfat_original.xml` — **base** FlexFat (`-fsanitize=flexfat`). The FlexFat
  analogue of the reference's `lowfat_original.xml`.
- `flexfat.xml` — **hardened** FlexFat, adds whole-access checking
  (`-mllvm -flexfat-check-whole-access`). Analogue of the reference's `lowfat.xml`.

Both deliberately drive **our** clang (`build/bin/clang`) via `-fsanitize=flexfat`
(NOT the reference's `-fsanitize=lowfat`; the internal pass flag is `-flexfat-*`,
not aliased to `-lowfat-*` — see STATUS.md Unit 10). Detection is keyed on
**exit signal 6 / SIGABRT** (`<bug_detected_exit_values>6`), matching the
reference's abort convention and the committed `lowfat_*.xml` configs (STATUS.md
"Exit-code convention").

## How it was run
```
cd MSET/build_mset
./mset --evaluate ../sanitizer_configs/flexfat_original.xml   # base
./mset --evaluate ../sanitizer_configs/flexfat.xml            # hardened
```
MSET emits a per-type verdict (`For <type>, the overall result is
DETECTED|UNDETECTED|PRECONDITIONS FAILED`). The detected set is diffed against the
committed reference oracle (`MSET/build/lowfat_original_detected.txt`,
`MSET/build/lowfat_detected.txt`).

## Result
The full bug-set diff and the classification of every delta live in
[`docs/STATUS.md`](../../docs/STATUS.md) under "Unit 11 — MSET differential".
Headline: **FlexFat's detected set is a strict subset of the reference's — zero
false detections, zero unexplained misses.** Of the 66 base-config deltas: **48 are
Part-II-scope deferrals** (Stack/Global lowfatification not yet done — 42 UNDETECTED
+ 6 PRECONDITIONS-FAILED, the latter unconstructable *today* only because of the
heap/normal address gulf, satisfiable again post-Part-II — see STATUS.md for the
layout proof), 12 are spatial-only (temporal), and **6 are the inherent LowFat
Heap→Heap offset-0 adjacency blind spot** (shared with the reference). Only the last
6 are not closed by finishing the planned scope.

## Note on the temporal phase
FlexFat is **spatial-only**; it detects no use-after-free / double-free. Several
MSET temporal test cases spawn detached worker threads that `sleep()` indefinitely,
so the evaluator's temporal phase does not terminate on its own. The **spatial**
differential (the meaningful comparison for a spatial checker) completes fully
before that phase; the runs above were stopped after the spatial verdicts were
emitted. Every temporal type is an expected, by-design miss.
