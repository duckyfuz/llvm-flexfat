# Stack-protection unit — notes (not yet started)

Forward notes for the FlexFat stack-protection unit (SPEC §II.1). Records the
hard prerequisite discovered during Unit 3.

## ⚠ BLOCKED ON: SHM support must land first
Stack mirroring cannot be built until the runtime has the shared-memory
machinery. Concretely it needs:
- **`lowfat_create_shm`** — create an anonymous, unlinked `/dev/shm` object
  (reference `lowfat_linux.c`: `O_EXCL` temp in `/dev/shm`, unlinked,
  `F_SETLEASE`, `ftruncate`), returning an fd.
- **`MAP_SHARED` same-fd region mapping** — each size-class stack region
  (`lowfat_stacks`) is `mmap`-ed `MAP_SHARED` to that one fd, so the *same
  physical bytes* are visible at every size-class address (SPEC §II.1,
  reference `lowfat.c:323-351`).

Unit 3's encoding-core init deliberately uses anonymous `mmap` + `mprotect` for
the SIZES/MAGICS tables and does **not** map stack regions or provide
`lowfat_create_shm` (see [STATUS.md](STATUS.md)). That shortcut is fine for the
tables but is exactly the mechanism stack mirroring requires, so **SHM support is
a hard prerequisite for this unit** — land it (as its own unit or the first step
here) before any stack work.

## What the unit then needs (SPEC §II.1, for later)
- Map the stack regions `MAP_SHARED` to the shm fd at init (the loop the Unit-3
  init skipped), plus the master stack region (`LOWFAT_STACK_REGION`).
- Pass side: `makeAllocaLowFatPtr` — sizeclass via `clzll(size)`, set alignment,
  replace oversized allocas, then `lowfat_stack_mirror(ptr, offset)` + RAUW.
- Stack tables `lowfat_stack_sizes/masks/offsets[]` are **already generated** in
  `lowfat_config.c` (Unit 2) — no generator work needed.
- Stack pivot (`lowfat_stack_pivot`) before `main`; escape analysis leaves
  escaping allocas native.
- Caveat (SPEC Part III): `MAP_SHARED` stacks also force `fork` interposition to
  avoid parent/child stack aliasing — track as a follow-on.
