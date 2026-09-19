# Muse handoff #1 - after G0 (2026-09-18)

You are implementing Riftwii, a clean-room Riivolution replacement for the
Wii. Read `docs/CONDUCTOR_REVIEW_2.md` (sections 4, 5, 6, 8) before starting.
G0 is done: the parser accepts the full documented format, the planner is
the strict layer, the engine composes patches, the licence is
GPL-3.0-or-later. Your job now is the disc/runtime side, host-first,
Dolphin-second, hardware last.

**Status (2026-09-18, updated by the conductor):** the host-only tasks were
done by the conductor while Task C waited on the user's Dolphin path and
disc dump. Do not redo them; build on them.

| Task | State | Commit | Where |
|------|-------|--------|-------|
| A - FST parser | done, agy-reviewed | `4f440c7`, `c211c28` | `include/riftwii/fst.hpp`, `src/fst.cpp`, `tests/fst_tests.cpp` |
| B - disc structures | done, agy-reviewed (no findings) | `934a474` | `include/riftwii/disc.hpp`, `src/disc.cpp`, `tests/disc_tests.cpp` |
| C - boot in Dolphin | **open, yours** | - | needs the user's Dolphin path + a disc dump |
| D - redirect table | done, agy-reviewed (no findings) | `f6a516a` | `runtime/rtable.{h,c}`, `include/riftwii/redirect.hpp`, `src/redirect.cpp`, `tests/redirect_tests.cpp` |
| E - FAT32 resolver | done | `4770248` | `include/riftwii/fat32.hpp`, `src/fat32.cpp`, `tests/fat32_tests.cpp` |

Deviations from the specs below, all deliberate: the redirect entry is
`{u64 vstart, u64 length, u64 source, u64 skip, u32 kind, u32 reserved}`
(40 bytes, no padding on either CPU) with kinds ZERO/MEM/SD/DISC - DISC
was added so a grown file relocated to the virtual window can still serve
its untouched bytes from their original disc offset; the C++ builder is
`redirect.*` not `rtable.*` because devkitPro's flat build directory would
collide `rtable.c` and `rtable.cpp`; the FAT32 resolver addresses
everything in 512-byte device blocks and also mounts through an MBR.
Confirm the FST size/max-size `>> 2` reading against a real disc at E1.

## Rules (non-negotiable)

- Never open Riivolution source, the `rawksd-2013` dump, or
  `D:\AI Projects\USB Loader GX Riiloaded\source\riivo*`.
- Allowed: wiibrew pages, libogc headers/sources, Brainslug
  (https://github.com/Chadderz121/brainslug-wii, MIT - you may adapt code
  with attribution in `NOTICE.md`), Dolphin (GPL-2.0-or-later, behaviour
  reference only; note the commit you read), the patch-format wiki.
- Every task: one commit, `-Werror` clean, host suites green
  (`cmake --build build-host && ctest --test-dir build-host
  --output-on-failure`), Wii DOL still builds (`make -f Makefile.wii` from
  the devkitPro msys2 shell, in the project root).
- Report per task: commit hash, files, commands + output, what is
  host-tested / built / Dolphin-verified / hardware-verified, next step.
- Ask the user only for: the Dolphin install path, a disc image they own,
  or a hardware run. Otherwise continue with the next task.
- Use `agy -p "<prompt>" --mode=accept-edits --dangerously-skip-permissions`
  for a review pass on each commit; read what it changed before trusting it.

## TASK A - Wii FST parser (host only)

`include/riftwii/fst.hpp`, `src/fst.cpp`, `tests/fst_tests.cpp`.
Parse a Wii FST image (12-byte big-endian entries: byte 0 = type
(0 file, 1 directory), bytes 1-3 = name offset into the string table that
follows the entries, u32 offset (files: disc offset stored `>> 2`),
u32 size (files) or next-entry index (directories); entry 0 is the root and
its "size" is the total entry count) into a tree. Explicit bounds checks:
name offset inside the string table, name NUL-terminated inside the table,
directory next-index within `[current+1, count]`, entry count consistent
with the image size, recursion depth cap, total-entry cap. API: lookup by
absolute path, lookup by bare file name (all matches, document order),
iterate a directory, and serialise back to bytes (round-trip byte-exact).
Fixtures are hand-built byte arrays in the test (no game data), including
malformed ones. Reference: https://wiibrew.org/wiki/Wii_Disc (FST section).

## TASK B - Disc structure parser (host only)

`include/riftwii/disc.hpp`, `src/disc.cpp`, `tests/disc_tests.cpp`.
Given a `ByteSource` of the raw (unencrypted view of the) disc:
- disc header at 0: game ID (6 bytes), disc number (0x6), version (0x7),
  Wii magic at 0x18 (`0x5D1C9EA3`); produce a `DiscIdentity`.
- partition table at 0x40000: four groups of {count, offset>>2}; each
  partition entry {offset>>2, type}; pick the first type-0 (game)
  partition.
- partition header: ticket at +0 (0x2A4 bytes), TMD size/offset(>>2) at
  0x2A4/0x2A8, cert chain size/offset(>>2) at 0x2AC/0x2B0, H3 offset(>>2)
  at 0x2B4, data offset(>>2)/size(>>2) at 0x2B8/0x2BC.
- TMD: required IOS = low 32 bits of `sys_version` at TMD+0x184; title id
  at TMD+0x18C.
- partition data header (decrypted view): main.dol offset (>>2) at 0x420,
  FST offset (>>2) at 0x424, FST size (>>2) at 0x428, FST max size (>>2)
  at 0x42C; apploader at 0x2440 (header: date 16 bytes, entry point u32,
  size u32, trailer size u32, then code).
All multi-byte values big-endian; all `>>2` fields widened to u64 before
shifting left. Hand-built fixtures, malformed cases (bad magic, partition
offset past end, TMD size absurd, FST max < size). Reference:
https://wiibrew.org/wiki/Wii_Disc and https://wiibrew.org/wiki/Apploader .

## TASK C - Boot an unmodified disc from Riftwii, in Dolphin (E1)

Frontend + a new `wii/boot.cpp`. Sequence (each step logs to the screen and
aborts with a message on failure - never launch after a failed step):
1. `DI_Init()`, wait for cover/disc, `DI_Mount()`, read the disc ID area;
   show game ID / region / version on screen.
2. Read the partition table and game partition header through
   `DI_UnencryptedRead` (0x8D; 32-byte aligned, multiples of 32).
3. Read the TMD, get the required IOS, `IOS_ReloadIOS(ios)`, re-init DI
   and re-mount. (Runtime IOS patches are a hardware concern - Dolphin
   does not need them; leave a clearly marked hook point for them.)
4. `DI_OpenPartition(offset)`, then `DI_Read` (0x71, offset in 4-byte
   words) the partition data header, FST (use TASK A to parse it), and the
   apploader.
5. Run the apploader: copy to 0x81200000, call `entry(&init,&main,&close)`,
   `init(report)`, loop `main(&dst,&len,&offset)` reading `len` bytes from
   partition offset `offset << 2` into `dst` until it returns 0, then
   `close()` returns the DOL entry point. Flush data cache / invalidate
   instruction cache over every loaded range.
6. Set the low-memory fields the SDK expects (game ID copy at
   0x80003180, IOS version fields, arena/MEM2 fields as the wiibrew Memory
   map lists them; verify by dumping 0x80000000-0x80003400 to
   `sd:/riftwii/lowmem.bin` before the jump), set the video mode for the
   disc region, shut down the GUI/audio/pads, `__IOS_ShutdownSubsystems`
   as Brainslug/libogc examples do, and jump to the entry point with
   interrupts disabled.
Also add a debug action "Dump file": choose a disc path (start with
`/opening.bnr` and `/main.dol`), read it through the FST and `DI_Read`,
write to `sd:/riftwii/dump/<name>`. This proves FST + DI reads and gives
the user legal test assets from their own disc.
Dolphin: Config > Paths > "Default ISO" = the user's dump; boot
`riftwii.dol`; the SD card image must be enabled. Pass = the game plays.
Adapt Brainslug's `src/apploader/` and `src/di/` under MIT with attribution
rather than rediscovering the sequence. Pin the Brainslug commit in
`NOTICE.md`.

## TASK D - Redirect table + freestanding walker (host only)

New `runtime/` directory, C99, compiled on the host with
`-ffreestanding -fno-builtin -nostdlib`-equivalent flags (a static library
target in CMake) and later for PPC. Define the table:
```
header: magic 'RWRT', u32 version=1, u32 entry_count, u32 sdio_fd,
        u64 disc_id/partition tag, u32 crc32 of entries
entry:  u64 virtual_start (bytes), u64 length,
        u8 kind (0 ZERO, 1 MEM, 2 SD), u8 pad[7],
        u64 source (MEM: address; SD: 512-byte sector), u64 source_skip
```
Entries sorted, non-overlapping. `rt_lookup(table, offset, length,
runs_out, max_runs)` splits a request into runs: PASSTHROUGH for gaps,
otherwise the entry kind with resolved source offsets. No allocation, no
libc. Then a C++ compiler on the host side: add `AppliedFile::flatten()`
that walks the composition chain and returns a sorted non-overlapping list
of extents (external file + offset, zero, or original disc + offset), and a
table builder that maps external extents to MEM or SD entries (for the
host test, use MEM with the external bytes loaded into memory). Test:
randomised reads through `rt_lookup` + the sources must equal
`AppliedFile::read` for the same file (including reads that straddle
several runs and the file end).

## TASK E - FAT32 fragment resolver (host only)

`include/riftwii/fat32.hpp`, `src/fat32.cpp`, `tests/fat32_tests.cpp`.
Over a `read_sector(lba, count, buffer)` callback: parse the boot sector
(bytes per sector, sectors per cluster, reserved sectors, FAT count, FAT
size, root cluster), walk a path with 8.3 and long-file-name entries
(UCS-2 to ASCII, case-insensitive match), return the file size and its
cluster chain as a fragment list `{start_sector, sector_count}`
(coalesced). Read-only. The test builds a minimal FAT32 image in memory
(boot sector, two FATs, root directory, a subdirectory, one contiguous and
one deliberately fragmented file with an LFN) and checks the fragments.
Bounds-check every FAT index and directory sector; cap chain length.

Order was A, B, C, D, E. A, B, D and E are done (table above); C is the
only open task and it needs the user's Dolphin path and disc image.
