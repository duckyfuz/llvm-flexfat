# FlexFat config

The FlexFat size/magic **table generator** and its golden outputs — a faithful
port of the reference LowFat `config/lowfat-config.c`. It is a standalone host
tool (no LLVM API surface) whose emitted files must stay **byte-identical** to
the reference, so the verification harness can diff against them.

## Layout
- `lowfat-config.c` — the generator. Build/run like the reference:
  `cc -O2 lowfat-config.c -lm -lpthread` then `./lowfat-config <sizes> <region-GB>`.
  Emits `lowfat_config.{c,h}` + `lowfat.ld` into the CWD.
- `sizes.cfg` — non-POW2 size classes (default; 61 regions @ 32 GiB).
- `sizes2.cfg` — POW2 size classes (CUSTOM; 30 regions @ 32 GiB).
- `lowfat.errs` — cached per-region precision errors for the non-POW2 variant.
  Computing these from scratch is billions of iterations; the generator reads
  this cache (matched by region-size/size/region-start) instead.
- `golden/pow2/`, `golden/nonpow2/` — committed reference outputs per variant.
- `test/` — the parity lit tests (wired into `check-flexfat`).

## Two variants (both must reproduce byte-identically)
- **POW2** (`sizes2.cfg 32`): magic `~(size-1)` (e.g. 16 → `0xFFFFFFFFFFFFFFF0`),
  `base = p & magic`, `error = 0` everywhere.
- **non-POW2** (`sizes.cfg 32`): magic = ceiling reciprocal `floor(2^64/size)+1`
  (e.g. 16 → `0x1000000000000001`), `base = ((u128)p*magic >> 64)*size`, with the
  per-region precision error baked into `lowfat_heap_select`'s `size <= SIZE-1-error`
  guard.

The fixed table addresses (`_LOWFAT_SIZES` @ `0x200000`, `_LOWFAT_MAGICS` @
`0x300000`), `LOWFAT_REGION_SIZE_SHIFT = 35`, the `__builtin_clzll`-based
`lowfat_heap_select()` dispatch, and the one-past-the-end `-1` are all part of
the ABI and reproduced exactly.

## Parity check
`flexfat/config/test/{pow2,nonpow2}-parity.test` rebuild the generator,
regenerate each variant, and `diff` byte-for-byte against `golden/`. Run via
`check-flexfat` (or `llvm-lit flexfat/config/test`).
