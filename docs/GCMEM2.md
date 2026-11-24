# GameCube games: RiftWii's code out of the game's memory

A design for giving GameCube games back the part of their 24 MB that
RiftWii's GameCube code takes today, by running that code from the Wii's
extra memory (MEM2) instead. Nothing here is built yet.

## Why

A GameCube game owns MEM1, the console's 24 MB. A real GameCube keeps only
the game's file table (FST) and the disc header's boot info (BI2) at its
top. RiftWii keeps more there, under the FST, and moves the game's arena top
(ArenaHi) down to make room:

| Piece | Size | Why it is in MEM1 today |
| --- | --- | --- |
| The blob: hooks, replay slots, C code, its context | 72 KiB (ISO), 88 KiB (RVZ), as built in November 2026 | the game's own branches jump into it, and a `b` reaches only 32 MB |
| BI2's copy | 8 KiB | a real GameCube keeps it too; Soulcalibur II's developer arguments need it lowest |
| DI registers' stand-in | 64 bytes (its address must end in 0x6000) | the game's patched disc accesses add 0x6000 to an upper half they load |
| Memory card struct, way back, code from IOS's counter, ARAM stubs | a few KiB | the game or the blob reaches them by `b` or a short address |
| libogc DI start slots | only for libogc games | branched to from the game's stores |
| FST | the disc's | a real GameCube has it there |

A tester's SpongeBob SquarePants: The Movie (PAL) shows the game's arena top
at 0x817E27E0, about 115 KiB under where a GameCube would leave it. A game
that sizes its heaps to what it had on a GameCube can come up short. That is
not proven to be the cause of that crash yet (see "First step").

## What makes it possible

- MEM2 stays mapped while the game runs. Broadway has four more BAT pairs
  (4 to 7) than the GameCube's Gekko, and a GameCube game's OS never writes
  them. libogc's BAT4 (0x90000000, cached, MEM2) is still on, and the
  GameCube ARAM already lives there (`GC_ARAM_BASE` in `gc_rt.c`), as do the
  blob's buffers at `kGcMem2Data`.
- RiftWii's plugins already run their code from MEM2 under libogc's
  mapping, so the instruction side of BAT4 maps MEM2 too. Whether that
  survives the game's OSInit (which rewrites BATs 0 to 3 and HID0) is the
  one thing to prove first, in Dolphin and on a Wii.
- The early exception stubs already jump to the blob through a full
  address (`lis`/`ori`, `rfi`), so they work with any address.

## The design

### Split the blob in two

- **Front (MEM1, small).** Only what the game's own instructions branch to
  or read by a short address:
  - one far veneer per entry point the loader patches a branch to
    (`lis r12,hi; ori r12,r12,lo; mtctr r12; bctr`, 16 bytes). Every hooked
    function is entered at its first word, where r12 and CTR are free;
  - the replay slots (the game's first instructions moved there, then a
    jump back into the game), which stay near the game: a moved
    instruction may itself be a relative branch, and those reach only
    32 MB;
  - the continue slots for the same reason;
  - the DI stand-in, the memory card's struct, the way back and the ARAM
    stubs as today.

  Expected size: about 1 to 3 KiB, depending on the hooks a game needs.

  The way back from the body to the front is far as well: a `b` from MEM2
  cannot reach MEM1, so the body enters a replay or continue slot through
  a full address (`lis`/`ori`, `mtctr`, `bctr`), which the loader writes
  with the front's address. The test that the blob is position independent
  (linked at two bases and compared) then covers the body alone; the jumps
  between the two halves are the loader's, checked by the split's own
  tests below.
- **Body (MEM2).** The C code, `gc_hook_common` and the assembly behind the
  veneers, and the context. The context stays found PC-relatively, as today,
  so it moves with the code. Only the code and the blob's own state are
  there; nothing the game's instructions read directly.

The plain (ISO) blob is position independent (linked at two bases and
compared), so its body needs no relocation: the loader copies it to its
MEM2 address and writes the front's veneers with that address. The RVZ
blob is not wholly: its Zstandard tables are reached through absolute
addresses that `LaunchGcNative` fixes with `apply_resident_relocs`
(`riftwii_gc_rvz.rel`). That step stays, run against the body's final
MEM2 address instead of the MEM1 one.

### One plan for MEM2

Today MEM2's pieces for a GameCube game are placed one after another in
`gcnative.cpp`. They become one host-tested plan (a `plan_gc_mem2` beside
`plan_gc_mem1` in `riftwii/gclayout.hpp`), with:

- the ARAM's 16 MiB at 0x90000000, as now;
- RiftWii's data (`kGcMem2Data`) and the body together, 32-byte aligned;
- everything else below IOS's part at the top left free, as one block.

Keeping RiftWii's pieces packed and the free part in one block is what lets
a later contract give that block to a mod that runs only in Wii mode,
without moving anything else again. That contract is deliberately not part
of this design.

### Caches

The body is code written by the loader: its range is flushed from the data
cache and invalidated in the instruction cache before the jump, as the
MEM1 blob is today.

## What it gives back

About 70 KiB of MEM1 for an ISO and about 85 KiB for an RVZ. MEM1's top then
holds the FST, BI2's copy, the small front, the DI stand-in and the few
KiB of the card, the way back and the stubs.

## Risks

- **The game's OSInit turns off BAT4's instruction side.** Then the body
  cannot run from MEM2 and the design fails at the first hook. The first
  step checks it.
- **A hook entered somewhere other than a function's first word** (a
  replay's middle, an exception path) where r12 or CTR are live. The far
  veneer clobbers them. Each entry point is checked against how the loader
  uses it; any that cannot use the veneer keeps a MEM1 copy.
- **Speed.** MEM2 is slower than MEM1. The hot paths are a disc read's
  start, a pad read once a frame and an AI DMA: a few hundred instructions
  each, from the cache after the first call. No visible cost is expected;
  the trace's timings will show it.
- **Games that write past MEM1's end or clear memory by size.** They never
  reach MEM2 (it is not in their address space), so nothing changes there.

## First step: measure before moving

1. **A GameCube memory squeeze for the test build.** `game_mem1_kib` in
   `test_limits.txt` takes that many KiB off the top of MEM1, as it does for
   Wii games. It is taken out above the FST, before the layout is planned,
   so the FST, BI2's copy and RiftWii's pieces all move down with the
   arena's top: a game that works out its memory from the FST's address
   (libogc) or BI2's (boot arguments) is squeezed too. RiftWii's pieces
   fill gaps, so the room taken is checked against the layout without it
   and grown until the arena's top is at least that much lower; the log
   gives the bytes it really took (the DI stand-in's address ending in
   0x6000 moves the layout in 64 KiB steps: 16 KiB asked took 64 in
   Dolphin). A squeeze that would leave no room is left out, and the log
   says so. With it, SpongeBob SquarePants: The Movie can show whether it
   crashes earlier with 100 KiB less, which tells whether MEM1 is its
   problem.
2. **A probe that BAT4 still runs code after OSInit.** The blob's first
   call after the game's OS is up runs one function from MEM2
   (`gc/test/mem2code`). It leaves `RUN!` in `gc_rt_cold.mem2_code_ran`,
   which Dolphin reads from memory. On a Wii the answer is what the player
   sees: the game goes on, or the crash screen shows an ISI at that MEM2
   address. In Dolphin first, then a test build for a tester.

Two small parts of the probe ship in release builds too, and are not part
of the move: `gc_rt_cold` (in MEM2, not the game's 24 MB) is one 32-byte
line longer for the probe's code and its mark, and `gc_on_pad_read` checks
`mem2_code[0] != 0` once per pad read. Only `gc/test/mem2code` puts code
there; the MEM1 squeeze is in the test build alone (`RT_TEST_BUILD`).

Only then the split itself.

## Testing the split

Dolphin, with the test build's limits, each game to its first gameplay and
through a save, the crash screen (`gc/test/crash`) and the way back:

- ISO and RVZ of the same game;
- Super Mario Sunshine, Luigi's Mansion, Melee, The Wind Waker;
- Pikmin 2 (jumps through the blob early), Soulcalibur II (BI2 arguments),
  Mario Golf: Toadstool Tour (stack at MEM1's top), Sonic Riders
  Tournament Edition (libogc), Paper Mario: TTYD (ARAM hold);
- a two-disc game and a game that loads its own ELF program (SpongeBob
  SquarePants: The Movie);
- Wii Remotes, the memory card, disc audio streaming.

Then a test build for testers on a Wii and a vWii.

## Results so far

- 22 November 2026, Dolphin, SpongeBob SquarePants: The Movie (USA,
  GGVE78) with a tester's settings (480p, 16:9, Wii Remotes on):
  - `gc/test/mem2code`: the test code in MEM2 (0x91231560) ran at the first
    pad read and left `RUN!` (0x52554E21) behind. MEM2 still runs code
    after the game's OSInit, in Dolphin. A Wii is still to check.
  - `game_mem1_kib = 128`: the arena's top at 0x817C2140 (128 KiB and 32 bytes under
    the usual 0x817E2160); 15 minutes of play in the first level, no
    crash. 30 minutes without the squeeze, no crash either. The tester's
    crash (PAL, GGVP78: a DSI reading 0x0C0D0F2C through a pointer that is
    not one) has not been seen here.
