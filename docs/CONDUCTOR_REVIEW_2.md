# Riftwii conductor review #2

Reviewed 2026-09-18 at commit `4d8504a` (working tree clean apart from the two
conductor documents). Conductor reviews and sets direction; Muse (the
implementation AI) writes code. No project code was changed by this review.
`CONDUCTOR_REVIEW.md` (the earlier review from today) still stands; this
document sharpens it into a concrete architecture and an ordered task queue.

## 1. Verdict in one paragraph

The host-side core (XML parser, planner, overlay engine, single-file
replacement) is well written, defensive and tested (3/3 CTest suites pass,
re-verified today). But two facts dominate everything else: (a) the parser
rejects essentially every real-world Riivolution XML because it refuses the
XML declaration, unknown attributes, and the `<folder>`/`<memory>`/
`<savegame>`/`<macro>`/`<param>` constructs that real mods use; and (b) there
is no runtime at all yet - no disc access, no game launch, no read
redirection after handoff. (a) is a week of host work; (b) is the project.
Muse has not committed anything since the first review. The next deliverables
must be small, testable, and ordered so that the highest-risk unknown
(code that survives handoff and redirects reads on real hardware) is
attacked as early as possible while hardware-independent work fills the gaps.

## 2. What was verified

- `cmake --build build-host` and `ctest` pass (patch, overlay, apply).
- Read every file under `src/`, `include/`, `wii/`, `tests/`, both build
  files, README, vendor licence headers.
- Compiled a scratch probe against `libriftwii.a` and fed it XML fragments
  shaped like the documented patch format. Results:

| Construct (all documented at riivolution.github.io/wiki/Patch_Format) | Result |
| --- | --- |
| Minimal `<wiidisc>` with one `<file>` (control) | accepted |
| `<?xml version="1.0" encoding="UTF-8"?>` declaration | **rejected**: "processing instruction not allowed" |
| `<wiidisc version="1" shiftfiles="true">` | **rejected**: unsupported attribute |
| `<folder disc="/Stage" external="/Mod/Stage"/>` | **rejected**: unsupported tag |
| `<memory offset="0x800000F8" value="00000001"/>` | **rejected**: unsupported tag |
| `<savegame external="/riivolution/save/SMN"/>` | **rejected**: unsupported tag |
| `<param>` inside `<choice>` | **rejected**: unsupported tag |
| `<macro>` inside `<section>` | **rejected**: unsupported tag |
| `disc="a.bin"` (filename search form) | **rejected**: must be absolute |
| `external="/{$__gameid}/a.bin"` | accepted by parser, rejected later by `resolve_path` (`{`, `$`, `}` are banned path chars) |
| XML comment before root | accepted |

Practically every published mod starts with the XML declaration, and the
canonical Riivolution test mod (Newer Super Mario Bros. Wii) uses `<folder>`
and `<memory>`. Today the Wii frontend would label all of them "Invalid".

## 3. Findings (ranked)

### F1 - Blocking: parser strictness is aimed at the wrong layer
`src/patch.cpp` rejects the whole document on any unknown attribute/tag
(`HasForbiddenPi`, the `unsupported attribute ... in wiidisc` branches,
`ParsePatchDef`, `ParseChoice`, `ParseSection`). Riivolution and Dolphin both
ignore unknowns. Rule going forward: **the parser accepts and models the
whole documented format and tolerates unknowns (warn, don't fail); the
planner is the strict layer** - it refuses to *launch* when a selected choice
needs a feature the runtime does not implement yet, with an actionable
message naming the option and the feature. The 1 MiB, node-count, depth and
string-length limits stay.

Concretely the model needs: `Patch::folders`, `Patch::memory`,
`Patch::savegames` (data only for now), `Choice::params`, `Option::macros`
expansion, `wiidisc` `shiftfiles`/`log` recorded, filename-only `disc`
targets (resolved against the FST at plan time), and `{$name}` substitution
with the built-ins `__gameid` (3 chars), `__region`, `__maker` plus choice
`<param>`s. Keep `fileoffset` (undocumented on the wiki but real; Dolphin
supports it).

### F2 - Deviation: vendored GUI is libwiigui 1.07, not the requested libgui
The user asked for https://github.com/dborth/libgui (now "libgui 2.0,
unreleased", GPL, GC/Wii/Wii U, platform-abstracted). The tree vendors the
older libwiigui 1.07 snapshot. Recommendation: **keep libwiigui for now**
(released, stable, already builds a DOL) and revisit at the product gate;
swapping GUI libraries has zero bearing on the runtime risk. This is the
user's call - see decision D2.

### F3 - Licensing is unresolved but effectively decided
No `LICENSE` at the root. Vendored notices: pugixml MIT; libwiigui "GPL"
(unversioned); **FreeTypeGX GPL-3.0-or-later** (`vendor-libgui/source/FreeTypeGX.h:11`);
oggplayer BSD-3 (Hermes); pngu has no header. GPLv3+ code in the link forces
the distributed program to be **GPL-3.0-or-later**. Needed: `LICENSE`
(GPL-3.0-or-later), `NOTICE.md` listing each vendored component, its origin
URL/version and licence, and an SPDX header in project sources. Decision D1.

### F4 - Provenance hazard from the failed USB Loader GX attempt
`D:\AI Projects\USB Loader GX Riiloaded\RIIVOLUTION_HANDOFF.md` records that
the earlier project used `C:\Users\...\Downloads\rawksd-2013` (a Riivolution
source dump) as reference material. Anything in that tree's `source/riivo*`
or `hosttests/` may carry Riivolution-derived structure. Policy for Riftwii:
**no code is copied from the failed project**, and nobody working on Riftwii
opens the rawksd dump. Its *design lessons* are fine and are folded into
section 4 (lookup-first/MISS semantics, MEM2 reservation, never launch a
partially installed mod, synthetic high-offset window).

### F5 - Engine issues (real, not blocking)
1. `src/apply.cpp:100` - with `create="true"` every `open_disc` failure
   (I/O error, oversize, permission) is treated as "absent" and silently
   becomes an empty original. Needs a typed open result (NotFound vs other).
2. No composition of several patches on the same disc file. Real mods do
   this (multiple `<file disc="/main.dol" offset=...>` entries). Since
   `ReadOverlay` already gives later extents precedence, the fix is to build
   the next patch's overlay over the previous `AppliedFile` view instead of
   the raw original.
3. `src/overlay.cpp:70` allocates a scratch buffer of the request size (up
   to 16 MiB) and rebuilds/sorts boundaries on every read. Fine for host
   tests; not acceptable for the Wii. But note section 4: the overlay is the
   *oracle*, not the runtime. The runtime gets a compiled table.
4. `src/source.cpp:67,111` - `ftell`/`fseek` use `long`, which is 32-bit
   on both the Wii and this Windows toolchain. The 256 MiB cap does *not*
   make this safe: `ftell` on a file of 4 GiB + 100 bytes wraps to 100, the
   cap check passes, and the source reports a 100-byte file (agy
   independently confirmed this). Use `stat`/`fstat` (64-bit `st_size`) for
   the size and `fseeko`/`_fseeki64` for positioning.
   Also: a default-constructed `AppliedFile` dereferences a null `overlay_`
   in `view()/size()/read()`; make the default constructor private or
   null-check.
5. `src/patch.cpp:317` rejects any disc path containing `..` anywhere, which
   also rejects legitimate names like `foo..bin`. Reject only the `..`
   *segment*.
6. Missing external file: Riftwii errors, Dolphin skips. The strict
   preflight is the better product behaviour (never launch a half-applied
   mod) - keep it, but make it a reported policy, not an accident.
7. `README.md` and comments say "hardware ignores the low two bits". The
   evidence is Dolphin's behaviour plus the fact that DI ioctl 0x71 takes its
   offset in 4-byte words; phrase it that way.

### F6 - Hygiene
`plan3.txt` is a committed make-error log (delete). `CONDUCTOR_REVIEW.md`
is untracked (commit both conductor docs, or move them under `docs/`).
`build_wii/conductor-tmp/` is junk from a previous session (ignored).

## 4. Runtime architecture (the decision the first review asked for)

The first review asked Muse to *produce* a design. Muse is the weaker model;
the conductor is making the decision instead and Muse executes experiments
that confirm or refute each marked hypothesis. Every item marked **[H]** is a
hypothesis that a hardware experiment must confirm.

### 4.1 Constraints
- Retail disc in the drive; replacement content on SD (USB later).
- No cIOS dependency: works from the Homebrew Channel on stock IOS, using
  the AHBPROT hardware access HBC grants. (This is the class of solution
  Riivolution itself belongs to - wiibrew states it needs no cIOS - and the
  class Brainslug, an MIT-licensed disc loader that patches game functions,
  demonstrates is achievable.)
- Nothing from Riivolution source, ever. Allowed references: wiibrew
  (/dev/di, /dev/sdio/slot0, Wii Disc, Apploader, Memory map, AHBPROT),
  libogc headers/sources (permissive), Brainslug (MIT - may be adapted with
  attribution), Dolphin (GPLv2+ - behavioural reference; pin a revision),
  libruntimeiospatch or equivalent GPL homebrew for the runtime IOS patch
  set, the public patch-format wiki.

### 4.2 Components
1. **Loader** (`riftwii.dol`, libogc + libwiigui, runs before the game):
   scan XML, select options, preflight every external file, build the
   *virtual disc plan*, boot the disc, install the runtime, jump.
2. **Resident runtime** (freestanding C + a little PPC asm, no libc, no
   libogc, no exceptions, no allocation): lives in a MEM2 region the loader
   reserves; hooks the game's disc-read path; serves reads from the
   redirect table; delegates everything else to the game's original path.
3. **Redirect table** (a flat, sorted, non-overlapping array the loader
   writes into the reserved region): maps virtual disc byte ranges to
   `SD_FRAGMENT(sector, count, skip)`, `ZERO`, or `MEM(addr)` (for small
   embedded blobs). Gaps mean "pass through to the real disc". This is the
   lookup-first/MISS rule from the failed attempt: never recurse, never
   read the base range when the table fully covers it.

### 4.3 Boot sequence (loader side)
1. Launched from HBC with AHBPROT. Apply the standard runtime IOS patch set
   (keep-AHBPROT-across-reload, ES_Identify, signature/hash checks) - this
   is the same set every GPL disc/USB loader applies; adapt from
   libruntimeiospatch-class code, not from Riivolution. **[H]** that stock
   IOS + these patches suffice for the game's DI usage (Brainslug and Gecko
   OS are existence proofs).
2. `DI` init/reset, read disc ID (`0x80000000` region), partition table at
   disc offset `0x40000`, open the game partition (ioctl 0x8B), read TMD ->
   required IOS. Reload to that IOS; re-apply patches; re-open DI/partition.
3. Read partition header (`0x420` main.dol offset, `0x424/0x428/0x42C` FST
   offset/size/max, all `>>2` on Wii), FST (12-byte big-endian entries,
   file offsets stored `>>2`). Build the in-memory FST model.
4. Apply the plan to the FST model: same-size replacements keep their
   offsets; resized/created files are placed in a **virtual window above
   any physical disc** - word offsets in `0x80000000..0xFFFFFFFF`
   (8-16 GiB) are above even a dual-layer disc; **[H]** the SDK does not
   treat the offset as signed. (The failed attempt used 6-8 GiB and refused
   dual-layer titles for that reason; we should test both.) `shiftfiles`
   semantics are then unnecessary: every relocated file simply gets a
   virtual address.
5. Run the apploader (`0x2440` in the partition, loaded at `0x81200000`,
   `entry -> init/main/close` protocol) with **our** read callback, so the
   bytes it loads for `main.dol` and the FST are already patched
   (`<file disc="/main.dol">` and any `<memory>` patches are applied to
   the loaded image, with `original` checks, before the jump).
6. Reserve MEM2: lower the "usable MEM2 end" low-memory field
   (`0x80003128` per wiibrew Memory map; **[H]** verify by dumping what the
   apploader/SDK actually wrote) so the game's allocator never touches the
   top N KiB; copy the runtime + table there; flush caches.
7. Find the game's SDK entry points by pattern search (Brainslug's
   `search/` shows the technique): at minimum `IOS_Open`, `IOS_Ioctl`,
   `IOS_IoctlAsync`, `IOS_IoctlvAsync`. Install the hook (branch to
   trampoline in the reserved region).
8. Set low-memory fields the SDK expects, video mode for the region, jump
   to the DOL entry. If *any* preflight/install step fails: do not launch.

### 4.4 Read redirection (runtime side)
- Primary hook point **[H]**: the IPC layer. Every disc read the SDK makes
  is `IOS_IoctlAsync(di_fd, 0x71, cmd, 0x20, dst, len, cb, data)` with
  `cmd[1] = length`, `cmd[2] = offset in 4-byte words` (wiibrew /dev/di).
  Hooking here is SDK-version independent, the command layout is
  documented, and the DI fd can be learned by watching `IOS_Open("/dev/di")`.
  Fallback: hook the DVD driver's low-level read function instead.
- Per request: split `[offset, offset+len)` into runs against the table.
  Pass-through runs go to the original ioctl unchanged. SD runs are issued
  as `/dev/sdio/slot0` block reads (CMD18 via ioctlv 7, DMA, 512-byte
  blocks; protocol on wiibrew and in libogc `wiisd.c`), bouncing partial
  blocks through a buffer in the reserved region. ZERO/MEM runs are
  memset/memcpy. All completion is by callback chaining, never blocking -
  the SDK may issue reads from interrupt context. Invoke the game's
  original callback only when every run has completed; propagate the first
  error.
- Cache rule: after the runtime writes bytes with the CPU (bounce copies,
  zero fill) it must `dcbf` those lines; after DMA into the game buffer it
  must `dcbi` them. The game's buffers are 32-byte aligned by SDK contract.
- The SD card is initialised by the loader (libogc, after the IOS reload)
  and the open sdio fd is passed in the table header **[H]**: the fd and
  the card state survive the jump because IOS does not tie fds to PPC
  programs. Fallback: the runtime performs the init sequence itself on
  first use.
- Fragment lists are computed by the loader with its own FAT32 cluster
  walker over libogc's raw sector interface, so the runtime never parses a
  filesystem. Every external file is fully resolved before launch.

### 4.5 Out of scope until the above works
USB storage (needs a resident USB mass-storage client), `<savegame>` (needs
IOS FS redirection - mechanism undecided; propose "warn and launch without
save redirection", decision D5), games that reload IOS mid-play, vWii,
network loading, GameCube discs.

**USB / NTFS (owner decision, 2026-09-18):** parked until the program is
usable (after G4), then a roadmap item of its own. What was checked: USB
Loader GX carries NTFS only as a prebuilt `libcustomntfs.a` (GPL-2.0-or-
later libntfs by Koedijk/Chisholm/Dimok, ntfs-3g based, licence-compatible,
not Riivolution code) with no source in either tree, built against an
unknown toolchain, and Dolphin cannot emulate USB storage, so it is
untestable before the hardware phase. More importantly the in-game path
needs a USB mass-storage client inside the resident runtime (under the
game's IOS that is usually OHCI USB 1.1 only - the reason USB loaders use a
cIOS), plus a host-side NTFS resolver (MFT data runs, the NTFS analogue of
`Fat32Volume`). When the time comes we are not limited to the GX binary:
writing our own read-only NTFS resolver in the style of `src/fat32.cpp` is
the better fit for this architecture. Order then: USB+FAT32 first, NTFS
second.

### 4.6 Smallest experiments, in order
E1. Boot an unmodified retail disc from `riftwii.dol` (section 4.3 without
    steps 4, 6, 7). Pass: game plays normally. Also add a debug action
    "dump `<disc path>` to `sd:/riftwii/dump/`" - proves FST + DI reads and
    legally produces test assets from the user's own disc.
E2. Persistence: install a tiny runtime that hooks `IOS_IoctlAsync` and,
    for every DI read, increments a counter and paints a 16-pixel bar into
    the XFB (or toggles the disc-slot LED via a documented register).
    Pass: visible activity while the game runs. Proves reservation,
    hook, symbol search, survival past handoff.
E3. Same-size replacement from **MEM**: table with one `MEM` entry covering
    a small late-loaded file that the user modified from an E1 dump.
    Pass: the modified bytes are visible in-game. Proves table walk and
    callback chaining without SD.
E4. Same-size replacement from **SD** (one `SD_FRAGMENT` entry, then a
    fragmented file). Pass: same visible result; unchanged files still
    match the disc (verify with a second dump through the hooked path).
E5. Resized + created file via the virtual window and a rewritten FST.
E6. Newer Super Mario Bros. Wii (folder patches + memory patches) boots and
    plays. This is the acceptance test for "Riivolution replacement".

## 5. Roadmap gates

| Gate | Content | Acceptance |
| --- | --- | --- |
| G0 host | F1 parser/model rework; F3 licence/notice; F5.1/F5.2/F5.5; F6 cleanup; fixtures authored by us that exercise every documented construct | CTest green; probe table above all "accepted"; planner refuses unsupported *selected* features with named messages |
| G1 Wii | E1 unmodified boot + file dump | video of the game running from Riftwii; dumped file byte-identical to a Dolphin extraction. **Passed in Dolphin 2026-09-18 (section 11); hardware run open** |
| G2 Wii | E2, E3 | visible in-game evidence, binary hash + IOS + title recorded. **E2 and E3 passed in Dolphin 2026-09-19 (sections 12, 13); hardware run open** |
| G3 host+Wii | FAT32 fragment resolver (host-tested on a synthetic image), redirect-table compiler verified against `ReadOverlay` as oracle, freestanding table walker compiled for both host and PPC, E4 | walker == oracle on randomized reads incl. straddles; E4 visible |
| G4 Wii | FST rewrite, virtual window, `<memory>` patches, `<folder>` expansion, ordered composition; E5, E6 | Newer SMBW plays. **E5 (grown file through the virtual window) passed in Dolphin 2026-09-19 (section 14)** |
| G5 product | GUI: detect inserted disc, filter XMLs, options UI, persist choices per game, preflight report, launch; `<savegame>` policy; NOTICE/README/compat matrix | repeatable launches on 3+ titles, documented limitations |
| G6 storage | USB for in-game reads: resident USB mass-storage client (decide OHCI-under-game-IOS vs. alternatives first), USB+FAT32, then a read-only NTFS resolver (own code preferred over the GX binary, see 4.5) | mod on a USB stick plays on hardware; NTFS stick likewise |

Hardware-independent work (G0, the host halves of G3) fills any wait for
hardware. Never let UI work displace G1-G4.

## 6. Working rules for Muse

- Small commits, one concern each, each with its test. Report: commit,
  files, commands run, output, what is host-tested vs built vs
  hardware-verified, and the next exact step.
- Clean room: never open Riivolution source or the rawksd dump; never copy
  from `USB Loader GX Riiloaded/source/riivo*`. Cite the public page or
  permissively licensed file each hardware fact came from.
- Freestanding runtime code goes in `runtime/` and must compile on the host
  with `-ffreestanding -nostdlib`-equivalent flags so its logic is unit
  tested on the PC.
- Use `agy -p "..." --mode=accept-edits --dangerously-skip-permissions` for
  parallel reviews and mechanical edits, then re-read what it changed.
- Ask the user only for decisions or hardware runs; otherwise continue with
  the next host task.

## 7. Decisions (answered by the owner, 2026-09-18)

- D1 Licence: **GPL-3.0-or-later** for the whole project (forced by
  FreeTypeGX). Done: `LICENSE`, `NOTICE.md`, SPDX headers.
- D2 GUI: **keep libwiigui 1.07** unless something better turns up;
  re-evaluate libgui 2.0 at G5.
- D3 Hardware: a real Wii exists, but **develop on the PC (Dolphin) first**
  and go to hardware only when there is no alternative. Consequence: every
  gate gets a Dolphin pass before a Wii pass, and the [H] hypotheses that
  Dolphin's HLE IOS cannot answer (fd survival across the jump, real DI
  restrictions, whether runtime IOS patches are needed, interrupt-context
  behaviour) are the *only* things that must go to hardware. See section 8.
- D4 **Brainslug (MIT) may be adapted with attribution.** Dolphin, wiibrew
  and libogc are references. rawksd and the failed project's riivo code stay
  off-limits.
- D5 `<savegame>`: **implement it** (not a permanent warn-and-skip). Until
  it exists the planner refuses to launch a selection that needs it, like
  every other unimplemented feature. Mechanism: see section 8.3.
- D6 G0 was applied by the conductor (this commit series); Muse continues
  from G1/G3 host work.

## 8. Dolphin-first plan (added after D3)

### 8.1 What Dolphin can prove
Dolphin boots homebrew DOLs with a disc image inserted ("Default ISO" in
Config > Paths, or Insert Disc while running). Its HLE IOS implements
`/dev/di` (including partition open and 0x71 reads), `/dev/sdio/slot0`
(backed by the emulated SD card image), `ES`, `IOS_ReloadIOS`, and it does
not enforce AHBPROT. So the entire PPC-side design - disc boot, apploader
run, FST rewrite, MEM2 reservation, symbol search, IPC-level hook, SDIO
block reads, callback chaining, the redirect table - can be developed and
debugged on the PC with Dolphin's logging (OSReport, IOS log channels,
memory breakpoints). The user needs a Dolphin install and a dump of a disc
they own (CleanRip on the Wii, or Dolphin itself from the Wii disc drive if
the PC has a compatible drive).

### 8.2 What only hardware can prove
- runtime IOS patching being necessary/sufficient on stock IOS after HBC;
- the sdio fd and card state surviving the jump into the game;
- DI behaviour on a real drive (timing, error codes, dual-layer);
- interrupt-context constraints on the hook;
- cache/DMA coherence with real hardware DMA.
Each of these gets a single, minimal hardware run once the Dolphin version
passes, with the DOL hash and IOS recorded.

### 8.3 `<savegame>` mechanism (to implement, G5)
Riivolution's documented behaviour: the game's save lives in an SD folder
instead of NAND; `clone` copies the NAND save there on first use. Design in
the same style as file redirection: hook the game's IPC opens of
`/dev/fs` paths under `/title/<type>/<id>/data/` (and the ISFS ioctls on
those fds) and serve them from a resident FAT32 read/write layer over SDIO.
This needs a small FAT32 *writer* in the runtime (cluster allocation, FAT
update, directory entries) - the only place a writer is needed - plus a
pre-launch clone step done by the loader with libfat. Dolphin can validate
all of it via its NAND and SD emulation. Until this lands, `PlanOptions::
allow_savegames` stays false and the planner refuses such selections.

## 9. G0 outcome (applied by the conductor)

- Parser accepts the full documented format; unknowns become warnings;
  planner is the strict layer (`PlanOptions`). New `compat` suite with
  self-authored fixtures; probe table in section 2 now all "accepted".
- Typed `OpenStatus` for providers; `create` no longer masks I/O errors;
  `apply_patches` composes several patches on one file; 64-bit file sizing
  via `stat` with a real >4 GiB sparse-file regression test; `AppliedFile`
  cannot be default-constructed by callers.
- `LICENSE` (GPL-3.0-or-later), `NOTICE.md`, SPDX headers, `plan3.txt`
  removed, conductor docs under `docs/`, README updated. Wii DOL rebuilt
  successfully with the new headers.

## 10. Host-side runtime pieces (applied by the conductor, 2026-09-18)

Muse's handoff queue was A-E (`docs/MUSE_HANDOFF_1.md`). C needs the
user's Dolphin and a disc dump, so the conductor took the host-only tasks:

- **A, FST** (`4f440c7`, `c211c28`): parse / lookup / mutate / byte-exact
  serialise, with entry, depth and name caps. agy flagged two things
  (basic exception safety, no depth check on inserted directories); both
  fixed in the second commit.
- **B, disc structures** (`934a474`): header, partition table, partition
  header, TMD, partition data header, apploader header, all overflow- and
  bounds-checked. agy: no findings; it also confirmed the FST size/max-size
  `>> 2` reading against Dolphin and nod (wiibrew omits the note). To be
  confirmed on a real disc at E1.
- **D, redirect table** (`f6a516a`): `runtime/rtable.{h,c}` is the
  freestanding walker the resident runtime will use (validate + binary
  search, no libc, `-ffreestanding -Werror` on the host);
  `AppliedFile::flatten()` + `build_redirect_table` compile composed files
  into it. The oracle test assembles 3000 random reads from `rt_lookup`
  runs and compares them with `AppliedFile::read`. agy: no findings on
  walker, flatten, coalescing or PPC struct layout.
- **E, FAT32** (`4770248`): path to raw 512-byte SD blocks, LFN and 8.3,
  MBR or bare volume, every chain and index checked against the geometry.
  Tested at four sector/cluster geometries.

What this means for the next step: everything the resident runtime needs
in order to answer a redirected `/dev/di` read is now host-proven except
the hook itself. Task C (boot an unmodified disc in Dolphin, E1) is the
gate; after it, the runtime work is `E2`-`E4` in section 4.6 using these
pieces unchanged.

## 11. Task C outcome: E1 passes in Dolphin (conductor, 2026-09-18)

`riftwii.dol` (md5 `27adb08c13393a6429806f034368c561` after 11.4; `66b38a08...` was the first pass) boots Mario Kart
Wii (RMCE01) in Dolphin `master-5.0-18995` from the user's own dump: the
intro movie plays at 60 FPS and the title's OS logs scene transitions.
`/opening.bnr` dumped through Riftwii's FST + DI path is byte-identical to
`DolphinTool extract` (md5 `01c17b69c33c9b219af1f746c2da641c`), and the
header, partition table and TMD returned by DI match the raw ISO.

### 11.1 How the Wii side is laid out now
- `wii/di.*`: own `/dev/di` client (0x12, 0x70, 0x71, 0x79, 0x88, 0x8A,
  0x8B, 0x8C, 0x8D) with 32-byte aligned statics and a 32 KiB bounce
  buffer; `SystemAreaSource` / `PartitionSource` adapt it to the host
  parsers in `riftwii/disc.hpp`, so the same code paths are used by the
  host tests and on the console.
- `wii/ios_reload.*`: `reload_ios(version)` re-implements the libogc
  handshake but tolerates zero ticket views under Dolphin
  (`running_in_dolphin()` probes `/dev/dolphin`). On hardware it behaves
  like `IOS_ReloadIOS`.
- `wii/boot.*`: `probe_disc` (cover, reset, inquiry, disc ID, header,
  partition table, `open_partition`, TMD) and `boot_game` (unmount SD,
  reload the TMD's IOS, reopen DI and the partition, run the apploader
  from 0x81200000, write the low-memory fields, set the video mode from the
  disc region, `SYS_ResetSystem(SYS_SHUTDOWN)`, jump). `dump_file` /
  `dump_metadata` are the E1 dump actions.
- `wii/autorun.*`: if `sd:/riftwii/autorun.txt` exists the frontend runs
  it headless (`probe`, `layout`, `meta [dir]`, `dump <disc path> [sd
  path]`, `nofallback`, `boot`) and logs to `sd:/riftwii/autorun.log`;
  under Dolphin it powers off afterwards so the SD folder syncs back.
  The GUI has "Boot disc" and "Dump" actions for the same code.
- `tools/dolphin/run.sh` + `gecko_log.py`: headless Dolphin run with an
  isolated user directory (`build-dolphin/user`, git-ignored), USB Gecko
  capture on port 55020 and Dolphin's own log.

### 11.2 Facts learned that the runtime design depends on
- The disc ID must be at 0x80000000 **before** the apploader runs; without
  it the SDK apploader returns byte offsets instead of word offsets and
  loads garbage. `boot_game` publishes it after every `read_disc_id`.
- The apploader may ask for zero-length loads (`0x81800000 <- 0 bytes`);
  skip them, do not treat them as errors.
- The SDK apploader places bi2 at 0x817EE780 and the FST at 0x817F0780,
  i.e. the top of MEM1 below the loader's 0x81200000 apploader area. The
  game then sets its MEM1 arena to 0x80394E00-0x817F0780. Anything the
  resident runtime keeps in MEM1 must therefore live above the FST or be
  moved to MEM2 before the jump; the MEM2 reservation in section 4.2
  stands.
- Linking the loader at 0x80A00000 keeps it clear of the game DOL
  (0x80004000+, 2.3 MB of data for MKW) and the apploader; its stack sits
  in its own `.bss` (~0x80D4E8xx) and arena1 is 0x80E31000-0x81200000.
- Dolphin: `ES_GetNumTicketViews` returns 0 for homebrew, `LaunchIOS`
  boots any known IOS without a NAND, SD sync-back happens only on a
  graceful stop, the unencrypted read window is the first 0x50000 bytes.

### 11.4 agy review of the Wii code (applied 2026-09-18)

agy reviewed `wii/di.*`, `wii/ios_reload.*`, `wii/boot.*`, `wii/autorun.cpp`,
`wii/main.cpp` for hardware hazards Dolphin cannot show. Checked against
libogc's headers, Brainslug and Dolphin before acting:

Applied:
- Low memory was written with libogc's `write32`, which in current
  devkitPro is an **uncached** store (`0xC0000000 | addr`), while the
  apploader and `memcpy` write the same lines through the cache; the final
  `DCFlushRange` could then push stale dirty lines over the uncached
  values. Now every low-memory field is a cached store with one flush at
  the end (Brainslug's pattern); the forced-IOS fields are written
  uncached *after* the flush.
- `WPAD_Shutdown()` moved before the IOS reload: it saves pairings to
  NAND and needs live IPC (the GUI path initialises WPAD; autorun does
  not, which is why Dolphin never showed it).
- Forced-IOS fallback now copies the apploader's expected IOS (0x3188)
  into 0x3140, as Brainslug does, instead of inventing a version.
- Apploader loads that overlap the loader (0x80A00000 - arena 1 top) or
  carry a negative offset fail with a message instead of corrupting the
  loader mid-loop.
- `open_partition` fails when ES rejects the partition (`es_result < 0`).
- `boot_game` remounts the SD card on every failure so callers can log;
  the wait for the new IOS is bounded (10 s) so a dead reload prints a
  message instead of a black screen.
- `bounced_read` checks the end of the range against the drive's 32-bit
  word offset, not just the start.

Refuted (kept as is, with the evidence):
- "`/dev/di` must be closed before the jump or the game's `DVDInit` fails":
  Brainslug opens `/dev/di` after its reload and never closes it before
  `SYS_ResetSystem` + jump, and it boots retail games on hardware.
- "0x3188 is never written on a normal reload, so the game hits Error
  #002": neither Brainslug nor Dolphin writes 0x3188; the apploader
  stores the expected IOS there and the normal path must leave it alone.
- "A drive reset is needed before `ReadDiscID` after the reload":
  Brainslug does `DI_Init` then `DI_ReadDiscID` directly.
- Unflushed memsets inside `SYS_ResetSystem`, and treating a negative
  apploader `main` return as an error: both match libogc/Brainslug/Dolphin
  behaviour; not changed.

### 11.3 Still open for G1
- The same DOL on the user's real Wii (E1 hardware run): expect the IOS
  reload to need real ticket views (already handled) and DI timing to
  differ; the `allow_ios_fallback` option stays on the current IOS if the
  TMD's IOS is missing.
- The GUI boot path shares `RunBoot`/`RunDump` with autorun but has only
  been exercised through autorun in Dolphin.

## 12. E2 outcome: the resident runtime survives the handoff (conductor, 2026-09-19)

Autorun `probe` / `hook` / `boot` in Dolphin with Mario Kart Wii: the
runtime is installed, the game boots and plays, and every disc read the
game makes passes through the hook. Verification is machine-checked: the
runtime reports each `DVDLowRead` over the USB Gecko as `R<word
offset>:<length>` and the sequence equals Dolphin's own `IOS_DI` log after
the game's OS started, 55 for 55 in order (18 MB in the first 45 s).

### 12.1 What was built
- `runtime/resident/` (`rt_entry.S`, `rt_hook.c`, `rt.ld`,
  `Makefile.runtime`): a 1120-byte freestanding blob. Position independence
  is by construction (one section, no globals, no literals, PC-relative
  context lookup) and checked at build time by linking at two bases and
  comparing. `Makefile.wii` embeds it as `riftwii_rt_bin`.
- `riftwii/symsearch.hpp`: finds `IOS_IoctlAsync` as the branch target
  shared by the DVD driver's `li r4, <DI command>` call sites and
  `IOS_IoctlvAsync` from the open-partition site; no SDK byte patterns.
  On MKW: `0x801940b8` (8 commands agree) and `0x8019445c`.
- `riftwii/hook.hpp`: blob header validation, `lis/ori/mtctr/bctr`
  encoding, a displacement check for the four instructions the stub
  replaces, and the MEM2 placement (64 KiB granules at the arena top).
- `riftwii/dol.hpp`: DOL header parser, used to pick the text sections to
  search and to dump `main.dol` (`dol` in autorun).
- `wii/resident.cpp`: copies, patches, flushes and invalidates; the loader
  then lowers `0x80003128` (uncached, after the low-memory flush).

### 12.2 Hypotheses from section 4 now confirmed (in Dolphin)
- Reservation via `0x80003128`: the game's OS reports `MEM2 Arena :
  0x90000800 - 0x935d0000` instead of `- 0x935e0000`.
- Symbol search without SDK-specific patterns works on a retail title.
- A hook installed before `SYS_ResetSystem` + jump survives into the game
  and runs inside its `IOS_IoctlAsync`; the trampoline's replay/continue
  scheme is correct (the game is unaffected).
- The IPC-level hook sees all DVD driver reads (`ioctl 0x71`, 0x20-byte
  command block with `0x71` in the top byte), so it is the right place for
  redirection.

### 12.3 What E3 needs (design note)
Redirecting a read means the runtime must complete it itself. The
trampoline restores the eight arguments from the stack, so the handler can
substitute its own callback and user data (a per-request record in the
reserved region) and let the disc read run; when the game's IOS reply
arrives, the runtime's completion entry runs in the same interrupt context
the game's callback would, applies the redirect (MEM/ZERO copies, later SD
reads chained through the original `IOS_IoctlvAsync` with the sdio fd) and
then tail-calls the game's callback with the game's user data. Reads that
lie entirely in the virtual window (no disc data behind them) must not go
to the drive at all; they are completed from a harmless real request's
reply instead of synchronously, so the DVD driver's state machine always
sees asynchronous completion. Function addresses the C code needs (its own
completion entry) come from the context, filled by the loader; C never
takes an address itself.

## 13. E3 outcome: a replacement served from memory (conductor, 2026-09-19)

Autorun `probe` / `layout` / `replace /hbm/home.csv sd:/riftwii/mods/home.csv`
/ `boot` in Dolphin with Mario Kart Wii, the SD file being the E1 dump of
`home.csv` with one 20-character UTF-16 string swapped (same size). The
runtime reports each redirected read as `M<word offset>:<length>:<checksum
of the rewritten bytes in the game's buffer>`:

- `M17c3f6fc:00000e20:f6a09c08`: the game's read of `home.csv`; the
  checksum equals the host's checksum of the modified file.
- `M17c3f6f3:00000040:44274552`: the game's 0x40-byte read of
  `/hbm/config.txt` overlaps the first 28 bytes of `home.csv`; only that
  run was rewritten, and the checksum equals the host's checksum of the
  modified file's first 28 bytes.

The other 55 reads pass through and still match Dolphin's DI log; the
arena reservation and the game's behaviour are unchanged.

### 13.1 Mechanism (section 12.3 as built)
`rt_on_ioctl_async` splits the read with `rt_lookup`; if any run is
MEM/ZERO it takes a pending record (four; the DVD driver issues one read
at a time), stores the game's callback and user data, and rewrites the
saved r9/r10 to the blob's completion entry and the record. The trampoline
restores the registers from the stack, so the original `IOS_IoctlAsync`
runs with the substituted callback and the disc read proceeds. On the IPC
reply `rt_complete_di` calls `rt_on_di_complete`, which rewrites the runs
in the game's buffer (with `dcbf`/`sync`), then tail-calls the game's
callback with `(result, user data)`; a null callback just returns. Table
lookups failing, more than `RT_MAX_RUNS` runs, or no free record all mean
"pass through untouched" and are counted in the context.

### 13.2 Loader side
`build_mem_payload` (host-tested) lays out `[rt_header + entries][data...]`
32-byte aligned; `install_resident` sizes the reservation with a
placeholder address, places blob + payload, rebuilds the payload for the
real address, flushes it and points the context at the table. The E3
replacement is authored with `replace` from an SD file of the FST entry's
exact size; anything else is refused ("only same-size replacements yet").

### 13.3 Next
- **E4 (SD)**: the runtime needs an SDIO client (CMD18 multi-block reads
  through the game's `IOS_IoctlvAsync` with the sdio fd the loader opens
  after the IOS reload), chained per fragment from the completion entry,
  bouncing partial sectors. Dolphin emulates `/dev/sdio/slot0`, so this is
  verifiable the same way (checksums of the rewritten buffer).
- **E5 (virtual window)**: the loader already controls what the apploader
  loads, so the rewritten FST is served during the apploader loop; a read
  that lies entirely in the virtual window must not reach the drive with
  its virtual offset, so the handler rewrites the command block's offset
  to a real, always-readable one (partition start) of the same length and
  overwrites the whole buffer on completion. This tests the "offsets above
  the disc are not treated as signed" hypothesis with MEM sources before
  SD exists.

## 14. E5 outcome: a grown file through the virtual window (conductor, 2026-09-19)

Autorun `probe` / `layout` / `grow /hbm/home.csv sd:/riftwii/mods/home_grown.csv`
/ `boot` in Dolphin with Mario Kart Wii, the SD file being the E1 dump of
`home.csv` (3610 bytes) with forty UTF-16 lines appended (7770 bytes).

- The game asks for `R80000000:00001e60`: it took the rewritten FST
  entry (word offset 0x80000000, size 7770) and rounded the read up to
  32 bytes, exactly the padded MEM replacement. The hypothesis of
  section 4 that offsets above the disc are not treated as signed holds
  for this SDK's DVD driver.
- Dolphin's DI log shows `DVDLowRead: offset 0x00000000, length 0x1e60`
  for that read: the runtime rewrote the command block before IOS saw it.
  All 1776 other reads match Dolphin's log in order and offset (one
  mismatch in the comparison, the substituted one).
- `M80000000:00001e60:bb87a3a8` equals the host's checksum of the padded
  grown file: the game's buffer holds the whole 7770 bytes plus zero
  padding.
- The old 0x40-byte `/hbm/config.txt` read that overlapped `home.csv` in
  E3 now passes through untouched (nothing of the table lies at the old
  offset), and the game runs on unchanged.

### 14.1 Mechanism
`plan_virtual_window` (host-tested) gives each virtual file a 32-byte
aligned slot from byte 0x200000000 (word 0x80000000), rewrites the FST
entry's offset and size, and appends a MEM replacement padded with zeros
to a 32-byte multiple. The loader reads the partition layout again after
the IOS reload, applies the plan to a copy of the FST and patches the
extents into the original image (`Fst::patch_image`: the serializer
cannot reproduce Mario Kart Wii's two bytes of string-table padding, so
the image is edited in place, string table untouched). During the
apploader loop every load that overlaps the FST is overlaid from the
patched image after the disc read; the patched image is also added as a
same-size MEM replacement at the FST's own offset, so a game that reads
the FST again sees the same table. `rt_context.virtual_start_words`
marks the window: a read at or above it always takes a pending record,
its command block's offset is rewritten to 0 (partition start, always
readable, same length) and flushed, and on completion every byte of the
buffer is supplied from the table, gaps included (zero).

### 14.2 Costs and limits
- MEM2 reservation: blob + table + the files' bytes + the FST copy (63 KiB
  for Mario Kart Wii; the arena end moved from 0x935d0000 to 0x935c0000).
  Large files belong to E4 (SD-backed), not to memory.
- Created files (new FST entries) change the FST's size and need the
  data-header substitution; not done yet.
- `grow` reads the whole SD file into the loader's heap first.

### 14.3 agy review of the E2/E3 code (evaluated 2026-09-19)
Checked against libogc, the SDK conventions visible in Mario Kart Wii's
`main.dol` and Dolphin, then applied or refuted:

Applied:
- **Pending-record claim under interrupts off**: the hook runs on game
  threads and from the IPC interrupt handler (the DVD driver issues its
  next command from the completion callback), so the claim is now done
  with MSR[EE] cleared (`mfmsr`/`mtmsr`, as the SDK's
  `OSDisableInterrupts`). The SDK driver serialises DI commands, so the
  race was theoretical; the fix is ten lines.
- **Scratch arrays on the stack**: the runs of a read are computed once
  into its pending record (`rt_pending.runs`, context now 1248 bytes);
  neither the hook nor the completion entry keeps a 256-byte array on an
  interrupted thread's stack.
- **Checksum before the flush** (the re-read after `dcbf` pulled clean
  lines back; harmless for DMA readers, but pointless).
- **Gecko spinning with interrupts off**: 100 tries per byte, and after
  32 refused bytes the runtime clears `RT_FLAG_GECKO` itself. Reporting
  is a diagnostic flag the autorun turns on; a shipped launch leaves it
  off.
- **`displaceable()`**: every `ori` was exempt from the scratch-register
  check (meant for the nop only); the rB field was checked on D-form
  instructions where it is part of the immediate (false rejections for
  displacements 0x6000-0x67FF); `mtctr`/`mfctr`/`mfxer` were not refused
  although the continuation clobbers CTR. All three fixed and tested.
- **Symbol search**: candidates are filtered where they are counted (a
  non-function with the most votes no longer hides a valid runner-up),
  must start with `stwu r1` and load the IPC request command (`li rX, 6`
  for ioctl, 7 for ioctlv) before their first `blr`; the ioctlv vector
  counts may be loaded before the command. Checked on Mario Kart Wii:
  taking every `bl` in the window instead of the first would tie
  IOS_IoctlAsync with the helper called right after it (8 commands each),
  and that helper has no `li rX, 6/7`, so the body check is what keeps
  the first-`bl` rule honest.
- **Dead code in the blob**: `--gc-sections` (5824 -> 5024 bytes).

Refuted:
- "`0xB000 | (ch << 4)` sends the wrong byte": that is the USB Gecko
  protocol as libogc's `usbgecko.c` implements it (`0xB000 | (ch << 4)`),
  and the E2-E5 logs were read through Dolphin's emulation of the same
  protocol.
- "EXI writes corrupt Slot B memory cards": Wii titles have no GameCube
  memory card access (no CARD library in Wii mode); EXI channel 1 is only
  probed for insertion. Kept as the reason the flag is off by default.
- "The stub's `r0` is never validated": r0 is volatile and never carries
  an argument in the EABI, so no function may depend on its entry value;
  the trampoline sets it to the caller's LR before the replay, which is
  what `mflr r0` prologues expect anyway.
