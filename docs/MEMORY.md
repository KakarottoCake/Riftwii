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

**The three tables below are history, not today's DOL.** They are from
upstream `main` at `5e9f5dd6` (the build before the stash), with
`powerpc-eabi-readelf -S`, `powerpc-eabi-nm --size-sort -S` and the linker
map (`build_wii/riftwii.elf.map`). Since then the menu font left the DOL
(an external `font.ttf`, 750 KiB), most of the unwinding and exception
tables went (`-fno-exceptions`, "Linked out" below), and the GameCube loader moved into its
plugin. For today's numbers, read the "Size report" step of CI's Wii build
(`tools/size_report.py`) or run the same commands on a fresh build. The
controller button pictures and the other items "Kept out of MEM1 until
used" (below) have since left too.

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
| Menu font (`wii/font/menu.ttf`) | 0 | Read from `apps/riftwii/font.ttf` once at start | **External file**, copied completely into MEM2, no font bytes in the DOL or stash. Replaces the former 310,920-byte core plus 384 KiB stream pool; see `wii/menu/fullfont.hpp`. | 750 |
| Six translations (`wii/lang/*.po`, zstd) | 166 | At start and when the language changes, one catalog | **Start-up stash** (built) | 165 |
| The GPL (`LICENSE`, zstd) | 11 | On the credits page | **Start-up stash** (built) | 11 |
| Menu sounds (`button_click`, `button_over`, `home_open`, `home_close`, raw PCM) | 226 | Played from memory by the DSP whenever the menu makes a sound | Stash next (played straight from the MEM2 copy, as theme sounds already are), not built: `vendor-libgui` names the sounds as linked arrays | 217 (all but the 8 KiB hover tick, which could stay) |
| Runtime blobs (`riftwii_rt`, `_rvz`, `_gc`, `_gc_rvz`, pad, shot, fault, wlog, vsd, code handler) | 173 | Copied into the game at a launch | The GameCube ones (`riftwii_gc.bin`, `riftwii_gc_rvz.zst` and `.rel`, 73 KiB) are the `gamecube` plugin's since stage (b) ([PLUGINS.md](PLUGINS.md)), and the six pad builds the `pads` plugin's (the DOL copies the one a game takes at the launch, [PLUGINS.md](PLUGINS.md#the-pads-plugin)); the rest stay: their MEM2 copy would have to survive everything a launch does in MEM2 | 73 (with the GameCube loader's code: 196 KiB in all) |

The main font is copied into MEM2 in full before the GUI starts. Korean is
supplied separately by `apps/riftwii/korean.ttf` (457 KiB, Noto Sans KR
under OFL 1.1), and also retained in MEM2 on first use. Korean's FreeType
face, temporary bitmap and glyph textures share the existing 384 KiB MEM2
pool; allocations that do not fit still use the heap. There are no open
font files to close on unmount or while an update replaces them. Historical
size tables above describe the older builds named in their headings.

The **start-up stash** keeps translations and the GPL in `boot.dol`,
outside RiftWii's resident image. Menu fonts are now external. `tools/dol_stash.py` adds
them after the link as one more DOL data section placed right above `.bss`,
which is where libogc starts the heap. crt0 copies the section to MEM2
before `.bss` is cleared (the wrapped `__CheckARGV`, `wii/core/stash.cpp`)
and erases its magic. From then on those bytes of MEM1 are ordinary heap.

### Files next to the DOL

The menu font is now external. `font.ttf` is already installed by older
in-app updaters before the new DOL. A missing file uses the Wii Menu's NAND
font, and libogc's built-in console can explain recovery when NAND is also
unavailable. This removes the earlier reason to embed the font.
Translations and the GPL remain in the stash; they cost no MEM1 after start.

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

### The 16 MiB line (`0x81000000`)

libogc's `SYS_Init`, which crt0 runs before `main`, boots the DSP by
copying 128 bytes of DSP code to `0x81000000` (where the DSP starts
from) and copies the old bytes back afterwards. On a real Wii the code
was still there when libogc next used that memory; Dolphin never shows
it. 2611-239 died on every console: about 3 KiB of new code moved
libogc's list of reset handlers (`sys_reset_func_queue`) from just under
the line to `0x81000014`, and `SYS_RegisterResetFunc` then followed
`0x029F0037`, the DSP code's sixth word, as a pointer (a DSI before the
session log opened). The same code linked 3 KiB lower started normally
on a tester's Wii.

So the line must never be left to chance. `Makefile.wii` used to link
`wii/core/mem1tail.o` after every library, which made Home's 128 KiB
scan stack the last thing in `.bss` and put the line inside it.

Since the passes below, the program ends about 1.9 MiB under the line,
and the line is in the heap. That is as safe: before `SYS_Init` copies
the DSP code, libogc writes only two things from the heap's start
(`__Arena1Lo`, the end of `.bss`): the command line and `argv[]`
(`__CheckARGV`), then its 64 KiB kernel workspace (`__lwp_wkspace_init`).
Nothing else above them is written before main's first `malloc`, which
hands the memory out as it finds it, as it already does the start-up
stash's dead MEM1 copy right there. So Home's scan stack now comes from
MEM2 at the first scan. `tools/check_mem1_line.py` fails the build unless
the line is at least 4 KiB above the end of `.bss` plus 128 KiB (64 KiB
for the command line, which no loader comes near, and the workspace).
Growth of about 1.8 MiB would bring the end back near the line: then a
buffer that nothing uses before `main` must be linked last again, big
enough to cover it (as `mem1tail.c` did; see the script's notes).

### Where the stash may load

The stash is dead before a launch starts, so the apploader may overwrite
it. What limits it is the loaders that load `boot.dol`:

- The Homebrew Channel refused a DOL section at `0x81300000` on a real Wii
  (the Wii test agent had to move down to `0x81100000`). Its exact limit
  isn't documented, so it is taken to be somewhere below that.
- The RiftWii channel's loader (`channel/common/dolboot.c`) accepts
  sections from `0x80003F00` up to `0x81700000`.
- Dolphin accepts anything, so it can't show this limit.

Since the DOL is packed ([ARCHITECTURE.md](ARCHITECTURE.md), "Memory"),
a loader no longer writes the stash or any of RiftWii's sections itself.
It sees two sections: the packed bytes right under `0x80900000` (from
`0x8059D140` today, 3.5 MiB) and the stub, whose code and workspace end at
`0x8093D9C0`. The stub then writes every section of the unpacked DOL, the
stash included, at its own address. So the highest address a loader is
asked to fill is now about `0x80940000`, far under the Homebrew Channel's
limit. `pack_dol.py` refuses packed bytes below `0x80200000`, where the
RiftWii channel's forwarders and their heaps lie. `STASH_LIMIT` still
holds for an unpacked DOL (`make -f Makefile.wii PACK=0`, and
`build_wii/riftwii-unpacked.dol`), which loaders still load as it is.

So the build fails when the stash would end past `STASH_LIMIT` in
`Makefile.wii`, which is `0x81200000`, the same line as the heap ceiling.
When the stash came in it ended at `0x811BC520`, where the program used
to end, so loaders saw the same highest address they already accepted.
By 21 Oct 2026 code and BSS growth had brought it to `0x811FF500`, 2.8
KiB under the line, which stopped pull requests that added code. The DOL
is now built with `-Os` (below), and the stash ends at `0x810A7380`
(build 2610-589), about 1.3 MiB under the line. Room under the line is
what code and BSS growth can use before the build stops. When it runs
out again, something has to leave the DOL (files or plugins), or the
limit has to be measured on the Homebrew Channel and raised.

### Built for size

`Makefile.wii` compiles the whole DOL with `-Os`. Against `-O2` for
everything, that took the stash's end from `0x811FF500` to `0x8109BC00`
(1.4 MiB; `pugixml.o`, `usbcatalog.o`, `patch.o`, `boot.o` and the
launch code gave the most). A few files keep `-O2` because the CPU, not
the card, the drive or the player, waits on their loops: the software
AES and SHA-1 (WAD contents, banners from a USB or SD image), the RVZ
junk generator (`rtrvz.c`), the launch's scans of the game's code
(`symsearch.cpp`, `codehook.cpp`, and after the hand-over
`videopatch.cpp`, `gamelang.cpp`, `gxpatches.cpp`) and the menu's
painters and PNG decoder (`skinpaint.cpp`, `canvas.cpp`,
`pngdecode.cpp`). They cost about 73 KiB together; the list and the
reason for each are in the comment above it in `Makefile.wii`. Measured
in Dolphin: with everything `-Os` the menu's art took 1009 ms to draw at
start instead of 850 ms, and a Brawl launch from disc ended 122 ms
later; with the list at `-O2` both are as fast as with `-O2` for
everything.

### Linked out

Two things the DOL carried and never used, 45 KiB of MEM1 together (on
main 21172fe0 the program's end went from `0x80ED37A0` to `0x80EC8280`):

- `.eh_frame_hdr`, 17 KiB: the sorted index of the unwinding tables
  that the prebuilt libraries (libstdc++, libogc, FreeType and the
  others) still carry since RiftWii's own code is built without
  exceptions. libgcc's unwinder here finds frames through the list
  `crtbegin.o` registers at start (`__register_frame_info`) and reads
  the index only through `dl_iterate_phdr`, which a DOL has not.
  `Makefile.wii` links with `--no-eh-frame-hdr`. (Checked in Dolphin
  before exceptions went, when it was 61 KiB: a `std::bad_alloc` thrown
  by `operator new` was still caught.)
- libc's `sscanf` and `fscanf`, 28 KiB: newlib's formatted input, for a
  handful of one-line files and an IP address. Those are read by hand
  now and RiftWii calls no scanf; the first call would bring about 13
  KiB back for `sscanf` and 13 more for `fscanf`. `siscanf` went too:
  newlib's time zone set-up read the `TZ` variable with it, which a Wii
  never has (`wii/core/timezone.cpp`, linked with
  `--wrap=_tzset_unlocked_r`).
- libc's `strftime`, which brought `sniprintf`'s formatter along: the
  five places that wrote a date use `date_text` (`src/data/datetext.cpp`).

Build 2610-734 (`213eb368`), measured with devkitPPC 16.1.0, had brought
`sscanf` back through the moved channel's cleanup-list reader. A further
size pass reads those exact lowercase paths by hand, shares `snprintf`'s
formatter with FILE and console output, and drops the prebuilt libraries'
unused exception tables:

| Change, measured separately | MEM1 heap gained | Packed DOL saved |
| --- | ---: | ---: |
| Read cleanup paths without `sscanf` | 14,080 bytes | 4,320 bytes |
| FILE output through `vsnprintf` and `fwrite` | 17,952 bytes | 1,952 bytes |
| Keep only libgcc's own unwind records | 124,096 bytes | 47,232 bytes |
| Total | 156,128 bytes (152.5 KiB) | 53,504 bytes |

The release program's end moves from `0x80F34C60` to `0x80F0EA80`, and
the stash's end from `0x80F88440` to `0x80F62260`. The unpacked DOL is
4,162,496 bytes (156,160 fewer; its file padding also shrinks by 32),
and the packed one 2,257,024. This leaves 3,085,696 bytes (3,013.4 KiB)
of MEM1 heap below the apploader before start-up allocations. In Dolphin,
the test DOLs on the same card start with `Memory (start): MEM1 248 KiB
taken, 2656 KiB free` against 2504 KiB free for 2610-734: the 152 KiB
above. The stash's bytes and MEM2 reservation are unchanged.

FILE output (`wii/core/streamprint.cpp`, `src/data/streamformat.cpp`) keeps
short messages on the stack, including the crash screen's lines. Longer
messages get a checked allocation and are written whole, including any
NUL bytes. `fwrite` takes stdio's stream lock, and the caller still decides
when to flush, so `logf`'s timestamps, truncation, batches and card sync
are unchanged. Host tests compare bytes and return counts with `vfprintf`,
including long lines, floats, NULs, buffering and a failed write.

None of the remaining unwind records was RiftWii's own: all came from
prebuilt libraries. `wii/core/noexcept.ld` discards their `.eh_frame` and
`.gcc_except_table` sections before the ordinary `rvl.ld` layout, keeping
3,012 bytes of libgcc's `unwind*.o` records and crtend's four-byte zero
terminator. Keep these when updating the toolchain: disassembly of this
16.1.0 build shows `uw_init_context_1` needs libgcc's own caller record or
it aborts before a throw can reach `terminate`. With those records kept,
`_Unwind_RaiseException` initializes normally, the missing record for
`__cxa_throw` returns `_URC_END_OF_STACK` (5), and `__cxa_throw` calls the
existing `terminate` handler. Frame registration and deregistration both
handle an empty table too (they return on a null pointer or a zero first
word), and the normal marker and terminator remain. The default handler
is still `abort`, which calls `_exit` and libogc's loader/reset path.
The menu crash report (`wii/core/crash.cpp`) and the in-game crash recorder
(`runtime/rtfault.c`) walk registers and the PowerPC stack's back chain;
neither reads exception tables. The Wii ELF still keeps its debug frames
for tools; those are not loaded into MEM1.

### The MEM2 copy

The copy goes to `mem::StashArea()`, about `0x90CF7000`, above the
restart snapshot (`0x90800000`–`0x90809000`), the main thread's stack
(`0x90809000`–`0x90829000`), libbte's and libwiiuse's buffers
and RiftWii's own, which follow the code and the read-only data back to
back from `0x90829000` (the end moves with each build; the linker checks it
stays below `0x90E00000`). In
MEM1 the DOL loads the code first above `.bss`, then the read-only data,
then the stash. It is 32 bytes of notes plus the stash (314
KiB today), and `mem::Init` starts arena 2 above it. Checked against the other users of MEM2:

- **IOS reloads** write below `0x90800000`, so they never reach it.
- **The menu's MEM2 heap**, covers and textures come from arena 2, which
  starts above the stash.
- **The plugin arena** ([PLUGINS.md](PLUGINS.md)) is taken from the top of
  the loader's MEM2 heap downward, at the far end from the stash: 384 KiB
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

### Start-up breadcrumbs and the saved NAND patch

The restart area's last half page holds two small records below the
stash, so a start that dies before the log can open still leaves a trace
for the next one (`mem::kStartupTrace` at `0x90808800`,
`mem::kNandPermissionNote` at `0x90808900`, inside `kRestartBytes` up to
`0x90809000`; the snapshot and handoff above them are capped by a
`static_assert`). IOS reloads write below `0x90800000`, the heap starts
above the stash, and a soft return to the Homebrew Channel keeps MEM2,
so both records survive exactly the starts that need them; only a
power-off clears them.

- The start-up note (`wii/core/startuptrace.cpp`, `include/riftwii/startupnote.hpp`) records each step reached before the log opens. Once
the log is open it reports `The last start stopped at: <step>` when the
previous start never got that far, then clears the note.
- The NAND permission note (`wii/launch/nandpermissions.cpp`) is written
before the running IOS's permission check is patched open, and the next
start closes a patch an interrupted run left behind before anything else
runs. Neither record is trusted on its own: the start-up note carries a
magic, a layout tag, the build version and a checksum; the NAND
permission note a magic and a checksum over the IOS number, the patched
word's address (inside IOS's code) and the instruction word before and
after, and the next start restores it only when the live word is exactly
the one RiftWii wrote.

### Kept out of MEM1 until used

A pass over what `.bss` and `.rodata` kept for the menu's whole life,
measured on the release DOL (devkitPPC 16.1.0) against `main` at
`4ef74f54`. The program's end moved from `0x81009360` to `0x80F82DE0`
(`.bss` 392 KiB smaller, `.rodata` 157 KiB, `.text` 30 KiB), 537 KiB
lower:

| What | Before | Now | MEM1 kept |
| --- | --- | --- | ---: |
| The MP3 player (libogc's `mp3player.o`) | 120 KiB of buffers and stack in `.bss` | RiftWii's own, `wii/menu/mp3stream.cpp`, on libmad's decoder: one MEM2 block taken at the first MP3 and kept | 125 KiB with its code |
| The Ogg player's decoder state and stack (`vendor-libgui/source/oggplayer.c`) | 57 KiB in `.bss` | One heap block at the first `PlayOgg`, kept for the next song | 57 KiB |
| The controller button pictures (`src/art/inputicons_data.cpp`) | 56 KiB of alpha masks | (count, value) runs, unpacked per drawing | 48 KiB |
| The Wii Menu music's player and producer stack (`wiimenusounds.cpp`) | 43 KiB in `.bss` | In the MEM2 block its stream buffers already used, taken when the theme first plays it | 43 KiB |
| The d2x USB check's sector (`check_d2x_usb_drive`) and the USB DVD drive's bounce buffer (`UsbRawDisc`) | 64 KiB in `.bss` | The check's for the check alone; the bounce buffer at the first read from a USB DVD drive | 64 KiB |
| The cover loader's and box art reader's stacks | 32 KiB in `.bss` | From MEM2 when each thread first starts | 32 KiB |
| One-off reads: the other loader's settings lines (`sharednandfind.cpp`), SYSCONF (`ReadPadPairings`), the entropy reads (`entropy.cpp`, still wiped before they are freed), a controller file's text (`hidpads.cpp`), the start-up button sampler's stack | 67 KiB in `.bss` | Taken for the read or the start-up and given back | 67 KiB |

In Dolphin, on the same card (the menu with Ogg music, then with an MP3
picked in Settings), `session.log` reads:

```
main (4ef74f54):  Memory (start): MEM1 260 KiB taken, 1672 KiB free
                  Memory (threads started): MEM1 1484 KiB taken, 448 KiB free
this pass:        Memory (start): MEM1 260 KiB taken, 2208 KiB free
                  Memory (threads started): MEM1 1484 KiB taken, 984 KiB free
```

Dolphin's audio dumps of both menus have the same tone at the same level
as `main`'s (440 Hz Ogg, 660 Hz MP3, the MP3's RMS 835.8 in both), no gap
but the 0.12 s where the lone MP3 ends and starts again (0.115 s on
`main`). A Mario Kart Wii launch with `gc_adapter = demo` logs the same
lines as `main`'s (`blob 18848 bytes at 0x817e9de0`, `hooked`) and the
game runs. Not tested on a Wii: the line in the heap (Dolphin never
showed the DSP problem), a USB DVD drive and a d2x USB launch.

Still in MEM1 and left as they are:

- libogc's and libasnd's own buffers (`__conf_buffer`, `audio_buf`, 16 KiB
  each): prebuilt, and the sound library's are read by the DSP.
- libwiigui's GX FIFO (`gp_fifo`, 256 KiB). A smaller FIFO stalls the CPU
  sooner instead of breaking, but the welcome dialog alone writes 97 KiB
  into it in one frame (measured in Dolphin from the FIFO's write pointer),
  so 128 KiB would leave little room on busier screens, and how much the
  menu's frame time suffers needs a Wii to measure. The graphics processor
  reads it, so it does not go to MEM2 untried either.
- Prebuilt libraries' tables: libmad's `rq_table` (32 KiB, in `.data`),
  Tremor's windows (`vwin*`, 30 KiB) and libogg's `crc_lookup` (8 KiB).
  Only rebuilding those libraries would move them.
- The Wiimmfi game list (`vendor-wwfc`, 12 KiB) and the embedded sounds
  (Ogg already, and `button_over_pcm`, 8 KiB, which zstd brings to 5.5).
- Floating-point `printf` (`_svfprintf_r`, `_dtoa_r`): the menu prints
  sizes and times with `%.1f`.

A second pass moved the program's end on to `0x80F6D480`, 86 KiB lower
again:

| What | Before | Now | MEM1 kept |
| --- | --- | --- | ---: |
| newlib's time zone parser and `strftime` | `siscanf`'s input engine, the multibyte reader, a 14 KiB Unicode table and `sniprintf`'s formatter | A fixed "no `TZ`" set-up (`wii/core/timezone.cpp`) and `date_text` (`src/data/datetext.cpp`) | 48 KiB |
| The Kirby fix's four tables (`src/patching/gxkirby.inc`) | 22 KiB, every word of every region | Super Kirby Clash's US words whole; the other regions as runs that shift them, and the one word that differs | 15 KiB |
| The three Wii Remote logger builds (`riftwii_wlog*.bin`) | 23 KiB in `.rodata` | One zstd frame (`riftwii_wlogs.bin`, 10 KiB), unpacked while a launch plans the hook | 13 KiB |
| The two WiiRD debugger handlers (`vendor-gecko/codehandler*.bin`) | 8.4 KiB in `.rodata` | One zstd frame (`riftwii_wiird.bin`, 2.7 KiB), unpacked only when a USB Gecko answers | 5.5 KiB |
| The list of games with no online play (`src/data/offlinegames.inc`) | 11.6 KiB, four characters a code | Three characters and a byte with a bit per region, plus the codes of other regions whole | 5 KiB |

A third pass moved it on to `0x80F3F1A0`, 185 KiB lower again (in
Dolphin, 2480 KiB of MEM1 free at the start and 1256 KiB once the menu's
threads run):

| What | Before | Now | MEM1 kept |
| --- | --- | --- | ---: |
| Home's scan stack (`wii/menu/homescan.cpp`) | 128 KiB in `.bss`, linked last to cover the 16 MiB line | From MEM2 at the first scan, kept; the line is in the heap (above) | 128 KiB |
| The mods' runtime and its RVZ build (`riftwii_rt*.bin`) | Two zstd frames, 24 and 35 KiB | One frame of both (`riftwii_rts.bin`, 38 KiB): they share most of their code. A launch unpacks both for its time | 21 KiB |
| The virtual SD card, screenshot and crash hooks (`riftwii_vsd`, `_shot`, `_fault.bin`) | 17 KiB uncompressed | One zstd frame (`riftwii_hooks.bin`, 10 KiB), unpacked while each hook plans; the install reads the header back from the copied blob | 7.5 KiB |
| The pad hook's two working copies of the USB controller catalog (`plan_pad_hook`) | 11.5 KiB of statics | Taken for the plan | 11.5 KiB |
| Three 4 KiB read buffers: the USB drive's first block, twice (`usbcatalog.cpp`), and a title's TMD view (`reportsend.cpp`) | 12 KiB in `.bss` | Taken for the read | 12 KiB |
| The shelf themes' backdrop textures (`gui_shelf.cpp`) | 5.5 KiB in `.bss` | From MEM2 at the first backdrop | 5.5 KiB |

A fourth pass moved it on to `0x80ECCEC0`, 456 KiB lower again (in
Dolphin, 2936 KiB of MEM1 free at the start and 1712 KiB once the menu's
threads run), by putting what prebuilt libraries kept in MEM1 into MEM2:

| What | Before | Now | MEM1 kept |
| --- | --- | --- | ---: |
| The main thread's stack (libogc's `default_stacks.o`) | 128 KiB in `.bss` | `mem::kMainStack` in MEM2: `wii/core/mainstack.cpp` defines the two weak pointers crt0 reads, so libogc's file is not linked. The exception stack stays in MEM1 (16 KiB), as exceptions begin without address translation | 128 KiB |
| libbte's packet pools and libwiiuse's state (`memb_mem_pool_pbufs`, `ram_block`, `__wpdcb`, ...) | 329 KiB in `.bss` | `mem::kLibraryBss` in MEM2: `wii/core/mem2sections.ld`, added to libogc's linker script, places their `.bss` there in a segment the DOL leaves out, and `__wrap___CheckARGV` zeroes it at every start | 329 KiB |

Both are above `kMem2Floor`, where an IOS reload never writes, and a
launch keeps memory patches off them (`boot.cpp`). In Dolphin a Wii Remote
connects through the stack as before, and a Mario Kart Wii launch with a
pack installs the runtime and every hook and the game runs.

A fifth pass moved it on to `0x80E0D520`, 766 KiB lower again (in
Dolphin, 3704 KiB of MEM1 free at the start and 2480 KiB once the menu's
threads run), by running the program's read-only data from MEM2:

| What | Before | Now | MEM1 kept |
| --- | --- | --- | ---: |
| `.rodata`: strings, tables, embedded pictures, sounds and blobs | 768 KiB for the program's whole life | `mem::kReadOnlyData`: linked there by `wii/core/mem2sections.ld`; `tools/dol_stash.py` makes the DOL load it just above `.bss` (the packed DOL's stub runs before libogc maps MEM2), and `__wrap___CheckARGV` copies it up before anything reads it, so its MEM1 bytes become the heap's. A marker and its size in its first two words tell a fresh start from a warm restart, which keeps the copy. libasnd's stays in MEM1 | 766 KiB |

In Dolphin the menu draws as before (screenshots before and after a warm
restart from a deliberate crash, which kept the MEM2 copy), a Wii Remote
connects, and a Mario Kart Wii launch with a pack runs.

A sixth pass moved it on to `0x80ADCCA0`, 3.2 MiB lower again (in
Dolphin, 6972 KiB of MEM1 free at the start and 5744 KiB once the menu's
threads run), by running the program's code from MEM2 the same way:

| What | Before | Now | MEM1 kept |
| --- | --- | --- | ---: |
| `.text`: the program's code, but libogc, the C library, libgcc, crtbegin/crtend and `restart.o` | 3.3 MiB for the program's whole life | `mem::kProgramCode` (`0x90951000`, 4 MiB): linked there by `wii/core/mem2sections.ld`, loaded by the DOL just above `.bss` ahead of the read-only data, and copied up by `__wrap___CheckARGV` before it calls anything there (the instruction cache is cleared over the copy). libogc maps MEM2 for instructions as for data (IBAT4). Calls between the two halves are beyond a branch's reach; ld adds long-branch stubs | 3268 KiB |

What stays in MEM1 and why: libogc runs before the copy (crt0's helpers)
and under every thread, exception and interrupt; the C library and libgcc
are called by it; `restart.o` makes the copy. crtbegin's and crtend's code
is called from `.init`, where ld can only put a long-branch stub inline:
the call then returns into the stub and runs the function again, forever
(the first try hung in `__register_frame_info` this way).

`tools/dol_stash.py` keeps the section holding the entry point as the DOL's
first text section, as elf2dol lists the MEM2 code first: the packed DOL's
stub finds crt0's command-line block (`_arg`) there, and without it another
loader's arguments would be lost.

In Dolphin the menu draws and plays its music as before (screenshots before
and after a warm restart from a deliberate crash, which kept the MEM2
copy), and a Mario Kart Wii launch with a pack installs the runtime and
every hook and the game runs.

A seventh pass moved it on to `0x80A57C00`, 532 KiB lower again (in
Dolphin, 7504 KiB of MEM1 free at the start and 6276 KiB once the menu's
threads run):

| What | Before | Now | MEM1 kept |
| --- | --- | --- | ---: |
| libogc's, the C library's, libm's and libgcc's code | 313 KiB, kept in MEM1 wholesale | In `mem::kProgramCode` with the rest. libogc's exception entry turns translation back on before it calls a handler, so only what runs before the copy or is called from `.init` stays: `restart.o`, libogc's `cache.o`, `early_init.o`, `exception.o`, `interrupt.o`, `thread.o`, `system.o` and its assembly, `memset`, crtbegin and crtend (18 KiB). `osrestart.o` had been kept by accident too: `*restart.o` matched it | 313 KiB |
| RiftWii's own zero-filled buffers: the disc and SD bounce buffers, the launch's TMD, the cover pool, the crash screen's fallback stack and report, and about a hundred smaller ones | 190 KiB in `.bss` | `.mem2bss`, zeroed at every start with libbte's. IOS reads and writes MEM2 as it does MEM1. Not the GX FIFO (`video.o`) or the exception stack (`mainstack.o`) | 190 KiB |
| libogc's buffers that only the CPU or IOS touch: `conf.o`, `wiilaunch.o`, `usbmouse.o`, `network_wii.o`, `gx.o` | 27 KiB in `.bss` | `.mem2bss`. Its EXI, SD Gecko and thread buffers stay, and so do libasnd's, which the DSP reads | 27 KiB |

`mem::kLibraryBss` grew from 352 to 576 KiB for them, which moves the
read-only data, the code and the stash's copy up by 224 KiB; all of it still
ends below `0x91000000`.

An eighth pass gave back 584 KiB of MEM2 (in Dolphin, 5528 KiB of MEM2
taken at the start, down from 6112) and 7 KiB more of MEM1. The code, the
read-only data and the zero-filled buffers each had a fixed slot with room
to spare (4 MiB for 3.5 MiB of code); they now sit back to back from
`mem::kProgramArea` in that order, each section's address taken from the
end of the one before it in `wii/core/mem2sections.ld`, and the stash's
copy starts at the 4 KiB boundary after them (`mem::ProgramAreaEnd()`, the
linker's `__riftwii_mem2_end`). The launch keeps patches off that one
range. libogc's thread blocks and lock tables, its memory card, EXI and
IPC bookkeeping, and the C library's, libsysbase's and libstdc++'s
zero-filled data joined RiftWii's in MEM2.

Still in MEM1 after these passes, about 350 KiB: the GX FIFO (256 KiB, see
above), libasnd's buffers (25 KiB, read by the DSP), the exception stack
(16 KiB, which `__wrap___CheckARGV` runs on while it clears the MEM2
buffers), `.data` and `.sdata` (22 KiB, which a warm restart puts back from
its snapshot), libogc's SD Gecko buffers (EXI), and 18 KiB of code. The two
framebuffers (about 600 KiB each) also come from MEM1 when the menu starts,
as the video interface scans them out.

### `riftwii.elf`

The stash is added to the DOL only. A `riftwii.elf` started directly has
none: it logs `Start-up data: MISSING` and leaves at the menu font. Start
the `.dol`. The ELF is still the one to keep for `addr2line`.
