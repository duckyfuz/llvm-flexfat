# FlexFat — status

Living status of the FlexFat reimplementation. Branch `flexfat/reimplementation`,
LLVM 23-dev (see [LLVM_NOTES.md](LLVM_NOTES.md)). Footprint: [INTREE_TOUCHPOINTS.md](INTREE_TOUCHPOINTS.md).

## Units landed
| Unit | Scope | State |
|---|---|---|
| 1 | Scaffolding: no-op pass, stub runtime, config skeleton, 4 test surfaces under `check-flexfat` | ✅ |
| 2 | Config/table generator (byte-identical to reference, POW2 + non-POW2) | ✅ |
| 3 | Runtime pointer-encoding core: `lowfat_index/size/magic/base/buffer_size`, tables @ `0x200000`/`0x300000`, region reservation, constructor/preinit; codegen parity (POW2 `and`, non-POW2 `mulq`, no `div`) | ✅ |

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
