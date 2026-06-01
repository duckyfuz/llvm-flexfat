# FlexFat config

This directory holds the FlexFat size/magic **table generator** and its golden
outputs — a port of the reference LowFat `config/lowfat-config.c`.

**Unit 1 status: skeleton only — no generation logic yet.**

When ported, the generator will take a size-class file plus a region size and
emit:

- `lowfat_config.h` — `_LOWFAT_SIZES` @ `0x200000`, `_LOWFAT_MAGICS` @ `0x300000`,
  `_LOWFAT_REGION_SIZE`, etc. (ABI-fixed addresses).
- `lowfat_config.c` — the `LOWFAT_*` macros, the `lowfat_sizes[]`/`lowfat_magics[]`
  tables, and `lowfat_heap_select()`.
- `lowfat.ld` — linker-script section placement for lowfat globals.

Two variants are supported (they share ~95% of the output and differ only in the
table contents):

- non-POW2 default (`sizes.cfg`, 61 regions, reciprocal magic `floor(2^64/size)+1`).
- POW2 CUSTOM (`sizes2.cfg`, 30 regions, bitmask magic `~(size-1)`).

These outputs must stay **byte-identical** to the reference, which is what the
golden-diff harness in [`test/`](test/) checks: regenerate the tables and `diff`
against the committed goldens. Unit 1 ships only a trivially-green sentinel
there; the real goldens land with the generator.
