# RiftWii's MEM1: what takes it, and the start-up stash

RiftWii (`boot.dol`) is linked at `0x80A00000` and must stay below the
game's apploader at `0x81200000` (`kMem1Ceiling`, below). Everything
between the end of the program and that line is RiftWii's MEM1 heap; when
it is full, the heap carries on in MEM2. `session.log` shows the room at
start:

```
Memory (start): MEM1 <taken> KiB taken, <free> KiB free; MEM2 ...
```

Upstream build 2610-454 ended at `0x811cc000` and had 4 KiB of MEM1 left at
start (208 KiB of room, 204 of them used before the menu). Stage (b) of
the plugins moved the GameCube loader out of the DOL: the program's end
went from `0x810F40A0` to `0x810C2FC0`, 196 KiB more heap at every start
([PLUGINS.md](PLUGINS.md#stage-b-as-built)). This page
measures where the 8 MiB went and records what the start-up stash moved
out of it.

The numbers below are from upstream `main` at `5e9f5dd6` (the build before
the stash), with `powerpc-eabi-readelf -S`, `powerpc-eabi-nm --size-sort -S`
and the linker map (`build_wii/riftwii.elf.map`).

## The sections

| Section | Start | End | KiB | What it is |
| --- | --- | --- | ---: | --- |
| `.init` + `.text` | `0x80A00000` | `0x80E3B440` | 4,333 | Code |
| `.rodata` | `0x80E3B440` | `0x80FFFA54` | 1,810 | Constants and every embedded file (below) |
| `.eh_frame_hdr`, `.eh_frame` | `0x80FFFAA8` | `0x81083964` | 528 | C++ exception unwinding tables |
| `.gcc_except_table` | `0x81083964` | `0x8109E241` | 106 | C++ exception tables |
| `.ctors`, `.dtors`, `.data`, `.sdata` | `0x8109E244` | `0x810A28C0` | 18 | Writable data (the restart snapshot copies it) |
| `.sbss`, `.bss` | `0x810A28C0` | `0x811BB680` | 1,124 | Zeroed at start: buffers, stacks, pools |
| Heap (to the ceiling) | `0x811BB680` | `0x81200000` | 274 | Of which libogc's start took about 66 KiB before `main` |

After the stash (this branch), `.rodata` is 927 KiB smaller, the program
ends at `0x810D4840` and the stash section sits from there to `0x811BC520`
until crt0 has copied it out.

## By area

The linker map's input sections, added up per source folder or library
(code, constants, unwinding tables and BSS together):

| Area | KiB | Mostly |
| --- | ---: | --- |
| `wii/menu` | 944 | code 665, constants 140 |
| The menu font, zstd (`menufont.zst`) | 750 | one file, read once at start |
| `wii/launch` | 585 | code 403, BSS 70 |
| `src/patching` | 547 | code 464 |
| libstdc++ | 490 | code 406, unwinding 53 |
| `wii/storage` | 398 | code 241, BSS 122 (32 KiB bounce buffers) |
| `vendor-libgui` | 379 | BSS 315 (the GX FIFO, 256 KiB) |
| libogc | 366 | BSS 176 (main and exception stacks), code 154 |
| libbte (Bluetooth) | 339 | BSS 290 (its buffer pools) |
| FreeType | 320 | code 258 |
| `src/fs` | 310 | code 267 |
| `src/data` | 287 | code 245 |
| newlib's libc | 246 | code 201 |
| Menu sounds, raw PCM | 226 | four sounds |
| `src/art` | 218 | code 136, constants 61 |
| `src/disc` | 182 | code 161 |
| Runtime blobs | 173 | copied into the game at a launch |
| Translations, zstd | 166 | six catalogs, one used at a time |
| `wii/online` | 123 | code 96 |
| Tremor (Ogg Vorbis) | 103 | the menu's music decoder |
| Everything else | 600 | `wii/hooks` 80, libwiiuse 79, the unwinding index 60, bearssl, pugixml, zstd, libfat, ... |

## The largest symbols

| Symbol | Kind | KiB |
| --- | --- | ---: |
| `menufont_zst` (the menu font, zstd) | data | 750.1 |
| `__EH_FRAME_BEGIN__` (all of `.eh_frame`) | unwinding | 467.8 |
| `gp_fifo` (libwiigui's GX FIFO) | BSS | 256.0 |
| `memb_mem_pool_pbufs` (libbte) | BSS | 200.6 |
| `s_mainStack` (libogc's main thread stack) | BSS | 128.0 |
| `home_open_pcm` | data | 93.8 |
| `home_close_pcm` | data | 84.4 |
| `ram_block` (libbte) | BSS | 64.0 |
| `__GNU_EH_FRAME_HDR` | unwinding | 59.9 |
| `private_ogg` (the menu music's decoder state) | BSS | 48.7 |
| `riftwii_rt_rvz_zst` (RVZ resident runtime) | data | 46.2 |
| `button_click_pcm` | data | 39.4 |
| `riftwii_gc_rvz_zst` (GameCube RVZ reader) | data | 36.9 |
| `__wpdcb` (libwiiuse) | BSS | 34.3 |
| Four 32 KiB bounce buffers (USB raw disc, d2x USB view, `wii::g_bounce`, `di::g_bounce`) | BSS | 128.0 |
| `boot_after_unmount` | code | 30.2 |
| `ko_po_zst`, `ja_po_zst`, `fr_po_zst`, `es_po_zst`, `it_po_zst`, `pt_po_zst` | data | 165.4 |
| `riftwii_gc_bin` (GameCube runtime) | data | 25.6 |
| `riftwii_rt_zst` (resident runtime) | data | 21.7 |
| `LaunchGcNative`, `RunAutorun`, `MenuSource`, `MenuHome`, `MenuSettingsSection` | code | 88.3 |
| `g_line` (20.0), `g_tmd` (18.5), `g_stack` and `g_fallback_stack` (16 each), `g_pool` (15.6) | BSS | 86.1 |
| `vwin8192` (Tremor), `s_excptStack`, `audio_buf`, `__conf_buffer`, `ReadPadPairings::conf` | data/BSS | 80.0 |
| printf and scanf's engines (`_vfprintf_r`, `_svfprintf_r`, `_svfwprintf_r`, `__svfscanf_r`, `__ssvfscanf_r`) | code | 67.3 |
| `TT_RunIns` (FreeType), `check_rvz` | code | 28.6 |

## Before and after, in Dolphin

The same SD card, the menu in Japanese (so the font and a catalog are both
used), `session.log`:

```
main (5e9f5dd6):  Memory (start): MEM1 168 KiB taken, 28 KiB free; MEM2 164 KiB taken, 44764 KiB free
with the stash:   Start-up data: 927 KiB in MEM2 at 0x90809020 (copied from the DOL at this start); its 927 KiB in MEM1 at 0x810d4840 are the heap's
                  Memory (start): MEM1 168 KiB taken, 952 KiB free; MEM2 1092 KiB taken, 43836 KiB free
```

After a warm restart (a scripted crash, then the crash screen's
automatic restart), the log reads `none in the DOL at this start, kept
from an earlier one` with the same figures, and the menu comes back in
Japanese with RiftWii's font.

## The embedded data

| Item | KiB in MEM1 | Used | Choice | MEM1 saved |
| --- | ---: | --- | --- | ---: |
| Menu font (`wii/font/rounded.ttf`, zstd) | 750 | Once at start (and after a warm restart), unpacked to MEM2 | **Start-up stash** (built) | 747 |
| Six translations (`wii/lang/*.po`, zstd) | 166 | At start and when the language changes, one catalog | **Start-up stash** (built) | 165 |
| The GPL (`LICENSE`, zstd) | 11 | On the credits page | **Start-up stash** (built) | 11 |
| Menu sounds (`button_click`, `button_over`, `home_open`, `home_close`, raw PCM) | 226 | Played from memory by the DSP whenever the menu makes a sound | Stash next (played straight from the MEM2 copy, as theme sounds already are), not built: `vendor-libgui` names the sounds as linked arrays | 217 (all but the 8 KiB hover tick, which could stay) |
| Runtime blobs (`riftwii_rt`, `_rvz`, `_gc`, `_gc_rvz`, pad, shot, fault, wlog, vsd, code handler) | 173 | Copied into the game at a launch | The GameCube ones (`riftwii_gc.bin`, `riftwii_gc_rvz.zst` and `.rel`, 73 KiB) are the `gamecube` plugin's since stage (b) ([PLUGINS.md](PLUGINS.md)); the rest stay: their MEM2 copy would have to survive everything a launch does in MEM2 | 73 (with the GameCube loader's code: 196 KiB in all) |

The **start-up stash** keeps these files in `boot.dol`, so the DOL alone
still works, but not in RiftWii's resident image. `tools/dol_stash.py` adds
them after the link as one more DOL data section placed right above `.bss`,
which is where libogc starts the heap. crt0 copies the section to MEM2
before `.bss` is cleared (the wrapped `__CheckARGV`, `wii/core/stash.cpp`)
and erases its magic. From then on those bytes of MEM1 are ordinary heap.

### Why not files next to the DOL

External files (`sd:/apps/riftwii/data/...`, read into MEM2 on first use)
would also save the DOL's size and the MEM2 the copies take. They were
weighed for each item and not built for now:

- **The font can't be external.** Without it RiftWii can draw nothing, not
  even the message saying the file is missing. The Wii Menu's font needs
  hardware access or a d2x cIOS, so it is not a fallback everyone has.
- **The other items gain nothing more in MEM1.** In the stash they already
  cost no MEM1 after start. As files they would bring missing-file and
  wrong-version cases, plus work in the release zip, the update pack, the
  in-menu update (which replaces only `boot.dol` today) and the channel
  (which starts `boot.dol` from the SD card or the USB drive).
- **Files win if the loaded image itself grows too big.** The stash still
  has to load below the limit. When code plus stash no longer fits under
  `0x81200000`, files (or plugins, [PLUGINS.md](PLUGINS.md)) are the next
  step, starting with the sounds and runtime blobs.

## The limits

### `kMem1Ceiling` (`0x81200000`)

`wii/core/memlimits.hpp`, added in 33b9dbcd ("Enforce the Wii's memory
limits and poison IOS reloads in Dolphin"). Every disc game's apploader
is loaded at `0x81200000` and runs while RiftWii is still running, so
RiftWii's code, data and heap must stay below it. The wrapped `_sbrk_r`
refuses heap growth past it. The address is fixed by the apploader format,
not chosen by RiftWii, so **moving it is not safe**: a heap block above it
would be overwritten in the middle of every disc launch. The other side of
RiftWii's 8 MiB, its link address `0x80A00000`, is where game DOLs stop
(ARCHITECTURE.md, "The loader's size and the game's room").

### Where the stash may load

The stash is dead before a launch starts, so the apploader may overwrite
it. What limits it is the loaders that load `boot.dol`:

- The Homebrew Channel refused a DOL section at `0x81300000` on a real Wii
  (the Wii test agent had to move down to `0x81100000`). Its exact limit
  isn't documented, so it is taken to be somewhere below that.
- The RiftWii channel's loader (`channel/common/dolboot.c`) accepts
  sections from `0x80003F00` up to `0x81700000`.
- Dolphin accepts anything, so it can't show this limit.

So the build fails when the stash would end past `STASH_LIMIT` in
`Makefile.wii`, which is `0x81200000`, the same line as the heap ceiling.
Today the stash ends at `0x811BC520`, where the program used to end, so
loaders see the same highest address they already accepted. The 270 KiB
left under the line is all the code and BSS growth can use before the
build stops. After that, something has to leave the DOL (files or
plugins), or the limit has to be measured on the Homebrew Channel and
raised.

### The MEM2 copy

The copy goes to `mem::kStashArea`, which is `0x90809000`, right above the
restart snapshot (`0x90800000`–`0x90809000`). It is 32 bytes of notes plus
927 KiB of stash, and `mem::Init` starts arena 2 above it (`0x908ED000`
today). Checked against the other users of MEM2:

- **IOS reloads** write below `0x90800000`, so they never reach it.
- **The menu's MEM2 heap**, covers and textures come from arena 2, which
  starts above the stash.
- **The plugin arena** ([PLUGINS.md](PLUGINS.md)) is taken from the top of
  the loader's MEM2 heap downward, at the far end from the stash: 256 KiB
  (`RWP_ARENA_BYTES`) just below where arena 2 ends, only once a plugin is
  used, and given back when the plugins shut down at the menu's close.
  #113's Reset restart snapshot copies MEM1 (24 MiB) into MEM2 at
  `0x91500000` up to about `0x92D00000`, inside the menu's arena-2 range,
  once a game runs. The `gamecube` plugin (stage (b)) outlives the menu:
  a GameCube launch loads it after "the menu closed" has given the arena
  back, and its code runs to the jump, through the writes of the card's
  image, the blob's buffers and that snapshot. The arena is at arena 2's
  top, above `0x92D00000` (it ends under IOS at `0x933E0000`), so none of
  them reaches it; the plugin checks this before it writes anything
  (`plugin_clear_of` in `plugins/gamecube/gchost.cpp`: its code more than
  one arena's size from `0x91000000`-`0x92D00000`) and stops the launch
  with a sentence saying so if it ever is not. Any other plugin that
  outlives the menu needs the same check.
- **IOS at `0x933E0000`** is far above.
- **The GameCube runtime** (`0x91000000` up: the memory card, the data
  buffers at `0x91200000`, the RVZ buffers at `0x91300000`, a later Reset
  snapshot from `0x91500000`) is above the stash.
- **A GameCube game's ARAM** (`0x90000000`–`0x91000000`) does cover it, but
  only once the game runs, as it does the restart snapshot. RiftWii never
  needs the stash after a handover: a game always returns to RiftWii
  through a fresh load.

The pack's memory patches keep off the copy (`boot.cpp`), as they keep off
the restart area: a launch that fails comes back through a warm restart,
and that restart unpacks the font again from the copy.

### Warm restarts

A warm restart re-enters crt0 without loading the DOL again. By then the
MEM1 bytes hold heap leftovers, and the magic crt0 erased after its copy
is gone, so crt0 keeps the MEM2 copy. Each copy carries the build's version
and a checksum. `stash::Check` tests both once per start, so a copy left
by another build, or one that was damaged, is never used.

### `riftwii.elf`

The stash is added to the DOL only. A `riftwii.elf` started directly has
none: it logs `Start-up data: MISSING` and leaves at the menu font. Start
the `.dol`. The ELF is still the one to keep for `addr2line`.
