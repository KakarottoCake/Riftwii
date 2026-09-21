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
| G3 host+Wii | FAT32 fragment resolver (host-tested on a synthetic image), redirect-table compiler verified against `ReadOverlay` as oracle, freestanding table walker compiled for both host and PPC, E4 | walker == oracle on randomized reads incl. straddles; E4 visible. **E4 passed in Dolphin 2026-09-19 (section 15); hardware run open** |
| G4 Wii | FST rewrite, virtual window, `<memory>` patches, `<folder>` expansion, ordered composition; E5, E6 | Newer SMBW plays. **E5 (grown file through the virtual window) passed in Dolphin 2026-09-19 (section 14); a `<file>`-only package runs end to end in Dolphin (section 17); created files, `<folder>` and `<memory>` patches, composition across packages and option choices pass in Dolphin (sections 18-20); `<savegame>` open** |
| G5 product | GUI: detect inserted disc, filter XMLs, options UI, persist choices per game, preflight report, launch; `<savegame>` policy; NOTICE/README/compat matrix | repeatable launches on 3+ titles, documented limitations. **GUI: disc identified, packages filtered, options set, choices kept per game, preflight report, launch; passes in Dolphin (section 22). Four titles from four SDK years run mods in Dolphin (section 23). `<savegame>` open** |
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
  data-header substitution; done in section 18.
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

## 15. E4 outcome: files served from the SD card (conductor, 2026-09-19)

Autorun `sdreplace /Boot/Strap/us/English.szs sd:/riftwii/dump/English.szs`
(the E1 dump of the file, 290147 bytes) and `sdreplace /hbm/home.csv
sd:/riftwii/mods/home.csv` (the E3 modified file), then `boot`, in
Dolphin with Mario Kart Wii:

- `M177ea3b6:00046d80:3b553c78`: the strap file, fetched in nine CMD18
  requests (eight of 64 sectors, one of 55, visible in Dolphin's IOS_SD
  log as DMA reads into the bounce buffer at 0x935b1c60); the checksum
  equals the host's checksum of the file.
- `M17c3f6fc:00000e20:f6a09c08` and `M17c3f6f3:00000040:44274552`: the
  E3 values again, now from the card (the second is the 28-byte overlap,
  a partial sector).
- The loader's own read-back before the handoff ("SD check") reports the
  same checksums; all 1490 disc reads still match Dolphin's DI log and
  the game runs on.

`sdgrow /hbm/home.csv sd:/riftwii/mods/home_grown.csv` (7770 bytes)
combines E4 and E5: `M80000000:00001e60:bb87a3a8`, the E5 checksum, from
a 16-sector CMD18 plus six zero bytes of virtual gap.

### 15.1 Mechanism
Loader, while the card is mounted: `resolve_sd_file` walks the FAT32
volume with the host-tested resolver over libogc's block reads and
`place_on_fragments` turns the file into sector runs; `build_payload`
emits one SD entry per run (sector, skip, length). After the IOS reload
the loader opens `/dev/sdio/slot0` itself (`wii/sdio.cpp`: reset,
status, select, 512-byte blocks, 4-bit bus, clock; only cards IOS already
initialised are accepted), optionally reads every run back for the
check, and leaves the card selected with the fd in the context together
with the game's `IOS_IoctlvAsync` (found in E2) and four 32 KiB bounce
buffers after the payload.

Runtime: the completion entry is now a state machine per pending record.
On the disc reply it applies MEM/ZERO runs, then for each SD run issues
SENDCMD CMD18 (request block, bounce buffer and response each in their
own cache lines inside the record, flushed before the call) through the
game's `IOS_IoctlvAsync` with itself as the callback; on each reply it
copies the chunk into the game's buffer, flushes, and issues the next;
when the last chunk has landed it tail-calls the game's callback with
the disc result. A refused or failed request completes the read with the
DI error code (2) so the DVD driver takes its error path rather than
using a half-filled buffer. The C code settles the result the game sees
(the entry passes it by address).

### 15.2 Assumptions to confirm on hardware
- The card stays selected across the handoff and the game's IPC accepts
  requests on a fd it did not open (true in Dolphin; IOS fds are per
  IOS instance, not per PPC "process").
- The SENDCMD vector form works on non-SDHC cards too when a buffer is
  given (libogc uses it in that case as well).
- CMD18 with 64 blocks per request through IOS's sdio module (libogc's
  unaligned path uses 8; its aligned path passes the caller's count).

### 15.3 What E4 does not cover yet
- Created files: the FST grows, so the data header the apploader loads
  must be substituted as the FST is (done, section 18).
- Cards IOS refuses at boot (the host-controller reset dance in libogc).

## 16. E7 outcome: the disc's own bytes through the runtime (conductor, 2026-09-19)

The last table kind, `RT_KIND_DISC` (what `build_redirect_table` emits
for the untouched part of a relocated or partially patched file), is
served the way SD runs are: a DVDLowRead of the 32-byte aligned span
into the bounce buffer, issued on the game's `/dev/di` fd (learned from
its first read) through the unhooked `IOS_IoctlAsync`, which is the
blob's replay slot: the four displaced instructions followed by the jump
to the original + 16. The drive's own error code, if any, is what the
game's callback receives.

Autorun `keep /hbm/home.csv` (the file moves to the virtual window with
its own bytes) in Dolphin with Mario Kart Wii: `R80000000:00000e20`, then
`M80000000:00000e20:38afe198`, the checksum of the original file plus
six zero bytes of window gap; Dolphin's DI log shows the substituted
read at 0 followed by the runtime's read at 0x17c3f6f8 of 0xe40 bytes.
The runtime now serves every kind the table compiler produces; the
XML-driven pipeline (`AppliedFile::flatten` -> `build_redirect_table`)
can be wired into the loader next, with created files (section 15.3)
the remaining structural gap.

## 17. A package end to end (conductor, 2026-09-19)

`wii/modplan.cpp` runs the host-tested pipeline on the console:
`read_package` -> `plan_package` (default choices, bare file names
resolved through the FST) -> `apply_patches` per disc file ->
`AppliedFile::flatten` -> `build_redirect_table`. The Wii supplies a
`ContentProvider` (disc side: the FST extent read through DI; SD side:
libfat, remembering each external source's path) and an
`ExternalPlacer` that resolves those paths to card sectors with the
FAT32 walker. A result of the original size stays in place; any other
size takes a slot in the virtual window and becomes an FST relocation
the boot applies to the FST the apploader loads (section 14). The
table's entries are SD, DISC and ZERO only (external bytes are never
copied into memory), merged with the loader's own pieces.

Autorun `xml sd:/riivolution/riftwii_test.xml` / `boot` in Dolphin with
Mario Kart Wii, the package holding three `<file>` patches:
- `/hbm/home.csv` <- a 7770-byte file: relocated, served from the card,
  `M80000000:00001e60:bb87a3a8` (section 15).
- `/Boot/Strap/us/English.szs` <- its own dump: in place from the card,
  `M177ea3b6:00046d80:3b553c78`.
- `/hbm/config.txt` <- 8 bytes at offset 0x10 from a dump of itself
  (`offset`, `fileoffset`, `length`, `resize="false"`): in place, only
  those bytes in the table, `M17c3f6f3:00000040:6369a501`, the checksum
  of the patched bytes.
The game runs on with every other read matching Dolphin's log. (A first
attempt patched `config.txt` with UTF-16 garbage and the Home Menu code
crashed parsing it, which is what applying that patch should do.)

### 17.1 Open for G4/G5
- Created files (`create="true"`): done, section 18. `<folder>`
  expansion (with `create`) and `<memory>` patches: done, section 19.
- `<savegame>` redirection: done in Dolphin, section 24.13.
- Option choices: done on the host and in the autorun (section 20); the
  GUI (G5) must present sections/options/choices and pass the selection.
- Hardware: sections 12-17 all rest on Dolphin; the hardware
  assumptions are listed in 15.2.

## 18. E6 outcome: created files (conductor, 2026-09-19)

A `<file create="true">` whose disc path does not exist now works end to
end, on top of section 17's pipeline.

### 18.1 Mechanism
- `Fst::create_file(path, offset, size)` (host-tested) adds the file and
  every missing directory on its path; existing directories match
  case-insensitively, as the SDK resolves paths, and a bad path leaves
  the table untouched. `compile_package` lets a missing disc file
  through when the first patch of its group has `create`, applies the
  patches to an empty file (which `apply_patches` already did on the
  host), gives the result a slot in the virtual window and emits an
  `FstRelocation` with `create` set.
- With a creation the boot cannot patch the FST image in place: it
  rebuilds the table (`Fst::serialize`, padded to 32 bytes) at the same
  partition offset, so the bytes after the original table must be free.
  The loader checks the grown range against the DOL image and every
  file the game still reads from the disc (relocated ones live in the
  window) and refuses otherwise. Mario Kart Wii has 1.5 GB of free space
  after its FST; a disc whose first file follows its FST directly would
  need the table itself moved into the window, which is not written.
- The apploader learns the FST's size from the partition data header it
  loads first (`load 0x812019c0 <- 32 bytes from word 0x00000108`, the
  fields at 0x420). The loader's overlay of apploader loads is now a
  list: the 16 encoded fields (`encode_partition_data_fields`,
  host-tested to round-trip through the parser: `fst_size` = the new
  size, `fst_max_size` raised to it when smaller) and the rebuilt FST.
  Both are also served as MEM replacements should the game read them
  again.

### 18.2 Dolphin run
The section 17 package plus `/hbm/created.txt` (35 bytes, from the
config.txt dump) and `/riftwii/e6/created.csv` (7770 bytes, two new
directories):
- `FST: 2 file(s) created, 63592 -> 63680 bytes (max 63592 -> 63680)`;
  `data header bytes 0x0-0x10 replaced`; the apploader then loads the
  table 64 bytes lower (`load 0x817f0740 <- 63680 bytes`) and its own
  8 KiB buffer moves down by the same 64 bytes, which is the apploader
  acting on the substituted size.
- The three checksums of section 17 are unchanged (`English.szs`, read
  after the shifted entries, `home.csv` from the window, `config.txt`),
  1762 game reads match Dolphin's DI log, no fault.

### 18.3 Open
- `<folder create>` (with `<folder>` expansion), and the FST-in-window
  fallback above if a disc ever needs it.
- The game never opens the created files here (nothing in Mario Kart
  Wii asks for them); a mod that does is the real test, on hardware.

## 19. Folders and memory patches (conductor, 2026-09-19)

The two remaining patch kinds of a package, both host-tested and both
run in Dolphin with Mario Kart Wii.

### 19.1 `<folder>` (riftwii/expand.hpp)
`expand_plan` turns a plan's folder patches into file patches before
`compile_package` groups anything, keeping document order with the
plan's own `<file>` patches. Semantics follow the patch-format wiki:
- A rooted `disc` pairs each file of the external folder with the disc
  file of the same name in that directory (case-insensitive, reported
  with the disc's spelling), walks subfolders that exist on the disc
  when `recursive` (the default), skips what has no counterpart, or
  creates it with `create` (missing directories included, through
  section 18). A rooted folder that is not on the disc is an error
  unless `create`.
- An empty `disc` is a filename search: every external file replaces
  every disc file of that name, anywhere; subfolders are not entered. A
  bare name is treated the same way (the wiki gives `recursive` a
  meaning only for rooted paths, and no meaning for a bare folder name).
- `resize` and `length` carry over to each file.
External entries are visited in ASCII-folded name order so the card's
directory order never matters. `ContentProvider` gained
`list_external`; the host `DirectoryProvider` and the Wii provider share
a dirent lister. Every existing disc path in a package is canonicalised
to the FST's spelling so `/HBM/x` and `/hbm/x` land in one group.

Dolphin: `<folder disc="/HBM" external="/riftwii/mods/hbm"
create="true"/>` (the E3 home.csv in place from the card and a created
file in a new subdirectory) plus `<folder external="/riftwii/mods/loose"/>`
holding English.szs: `M17c3f6fc:00000e20:f6a09c08`,
`M177ea3b6:00046d80:3b553c78`, the FST grown by one entry, 1769 reads
matching Dolphin's log.

### 19.2 `<memory>` (riftwii/mempatch.hpp)
Applied in the loader once the apploader has loaded the game and before
the runtime is installed (so the runtime's structural search sees the
patched code), through a `MemoryAccess` interface the host tests drive
against a buffer. Semantics from the wiki, with Dolphin's independent
implementation as the behavioural tie-breaker where the wiki is silent:
- plain: `value` at `offset | 0x80000000`; with `original` given and
  different, skipped (a note, not an error).
- search: the first place in the DOL's sections as the apploader loaded
  them (in load order, `align` stride) where `original` matches gets
  `value`, which must be the same length. Dolphin scans all of MEM1 in
  a second pass after the load-time pass, which can patch a second
  occurrence; Riftwii patches one.
- ocarina: the first occurrence of `value` in those sections (4-byte
  steps, as code), then the next `blr` at or after it (4-byte steps,
  starting at the match itself, as Dolphin does) becomes `b offset`
  (range-checked, no link bit).
- `valuefile` is read while the card is mounted (1 MiB cap). The patches
  run last, after the low-memory globals, so they win over them as in
  Dolphin. Writes are confined to MEM1 minus the loader's own image and
  the runtime's hook stub, and to MEM2 below the arena end (the
  runtime's reservation is above it). A package with only memory
  patches boots without the runtime.

Dolphin: the OS banner strings the game prints through OSReport were
patched three ways and Dolphin's log shows all three (`Kernel BUILT`
by a plain write with a matching `original`; `Riftwii Type` from a
valuefile with a prefix-less offset; `Firmware RW` by a search patch at
stride 4); a second plain write whose `original` no longer matched was
skipped; an ocarina patch on a unique 8-byte code pattern rewrote the
following `blr` at 0x80012738 into `b 0x80012750` (another `blr`, so
behaviour is unchanged) and the game ran on.

### 19.3 Open
- `<savegame>`: the only patch kind not executed (since done: section 24).
- The GUI (section 17.1).
- Hardware: nothing since section 11 has run on a console.

## 20. Composition across packages and option choices (conductor, 2026-09-19)

Booting the three test packages together exposed the gap: each was
compiled on its own, so two packages touching `/hbm/home.csv` produced
two table entries for one file and the boot refused them ("replacements
overlap in the virtual partition", the right answer to the wrong
input). `compile_packages` now takes every package at once: each is
parsed, planned and expanded on its own, then all file patches are
grouped by disc file in package-then-document order and applied as one
chain, so a later package's patch sees the earlier one's result exactly
as a later `<file>` inside one package does. Memory patches are
concatenated in the same order. The autorun recompiles the accumulated
list on every `xml` and logs the combined result.

Choices: `riftwii::select_choice(package, "Section/Option" or "Option"
or the option id, choice name / 1-based number / off)` on the host,
exact then case-folded, refusing ambiguity; `PackageSelection` carries
a package's choices into `compile_packages`; the autorun's
`set <option>=<choice>` applies to the last `xml`. The GUI (G5) will
build the same `PackageSelection` from its menus.

Dolphin:
- The three packages together: package 1 grows home.csv to 7770 bytes,
  package 2's folder patch replaces it with the 3610-byte E3 file, so
  the final layout is in place from the card
  (`M17c3f6fc:00000e20:f6a09c08`), three files created, five memory
  patches applied, 1758 reads matching Dolphin's log.
- A package with a default-off option set to its second choice and a
  default-on option disabled: the OSReport banner shows `Loader Type`
  and an untouched `Kernel built`.

## 21. agy review of sections 18-20 (evaluated 2026-09-19)

Applied, each verified first:
- Memory patches ran before the low-memory globals were written; Dolphin
  writes those globals before the apploader and nothing rewrites them
  after its patches, so the patches now run last. The loader's own image
  (0x80A00000 up to its arena) and the runtime's 16-byte hook stub are
  no longer writable; search and ocarina patches see the DOL sections
  only, not the FST or the apploader's buffers.
- A search patch read from a `valuefile` bypassed the parser's
  same-length rule; checked at read time and again when applying. An
  `align` above 32 bits could wrap on the console; clamped.
- `Fst::create_file` could leave new directories behind when the file's
  offset was unencodable; the offset is checked first now.
- Folder expansion resolved every entry from the FST root; a
  directory's children are now listed once. Trailing slashes and the
  root as the external folder are handled.
- A bare `<file>` name with several disc matches was refused; every
  match is patched now, as a by-name `<folder>` does.
- Files of 0 bytes (mods null videos this way) were refused; allowed. A
  32-bit size could wrap in the window cursor; widened. Relocations
  look the path up case-insensitively as a fallback.

Refuted:
- "The ocarina blr scan must start after the pattern": Dolphin's
  implementation starts at the match address, and the wiki's "next
  occurring blr" reads naturally from the match; kept, with the
  pattern now sought at 4-byte steps like Dolphin's.
- "`disc="/Stage/"` breaks expansion": the parser rejects trailing
  slashes on disc paths before a plan exists; stripped anyway for plans
  built in code.
- A null check on `encode_partition_data_fields`'s output pointer: an
  internal API whose callers pass arrays; not added.

## 22. The frontend (conductor, 2026-09-19)

`riftwii/launch.hpp` holds the frontend's state as plain data, so the
host tests cover it: `LaunchModel::add` parses each package and marks it
for the disc or not (`DiscFilter::matches`), `set_enabled` refuses what
is invalid or for another disc, `cycle` steps an option through its
choices and off, `selections()` yields one `PackageChoices` per enabled
package with every option stated (a package's defaults never leak past
the frontend), and `save`/`restore` keep the state in a tab-separated
file per game, ignoring what no longer exists. `select_choice` now
tries every `/` split so a section or option name holding one still
resolves.

`wii/frontend.cpp` is the card side shared by the GUI and the
autorun's `launch` command: `IdentifyDisc` probes without waiting for a
disc (an empty drive is a status line, not a wait), `ScanPackages` lists
`sd:/riivolution` and restores `sd:/riftwii/choices/<game id>.txt`,
`SaveChoices` writes it back. The GUI (`rift_menu.cpp`): the home
screen lists packages as On / Off / Other disc / Invalid, A toggles, +
opens the options screen (A next choice, - previous, B back), Launch
saves the choices and returns the selection to `main`, which runs
`RunLaunch` (compile with `compile_packages`, boot with the resident
when the result needs it, Gecko reporting off); with nothing enabled
Launch boots the plain disc. Dump keeps its hotkey and lost its button
(three template buttons fit a 640-wide screen at 0.85 scale, four do
not).

Dolphin: with the frame dump on (`[Movie] DumpFrames`), the home screen
renders the identified disc and the four test packages; with a choices
file enabling two of them and setting `Console banner` to `Loader`, the
screen shows On/On and the autorun's `launch` compiles exactly those
two (3 table entries, 1 relocation, 1 memory patch), the game boots
with the folder patch's files from the card and prints `Loader Type`.
The pad path itself (A/+/Launch) is untested: Dolphin input movies
(`-m`, a hand-written DTM with GameCube pad presses) never reached the
loader in this harness, so the buttons wait for hardware.

The preflight screen followed (commit 94b6a85): Launch now compiles the
selection first (`CompileSelection`) and shows the compiler's warnings
and notes one per row with a summary line (`N file range(s), N relocated
or created file(s), N memory patch(es)`), or the compile error with no
Boot button; Boot hands the compiled result to `main` (`BootCompiled`),
so nothing is compiled twice and nothing irreversible happens before the
report.

### 22.1 Open
- `<savegame>` (section 8.3): done in Dolphin, section 24.
- Every section since 11 rests on Dolphin; other titles: section 23.

## 23. Other titles; the runtime's code moves to MEM1 (conductor, 2026-09-19)

Three more discs, chosen for their SDK years, all with the same
`riftwii.dol` and the plain `hook`/`boot` autorun first (every DI read
reported over the Gecko and compared with Dolphin's DI log):

| Title | SDK (kernel / apploader build) | IOS | Plain hook |
| --- | --- | --- | --- |
| Wario Land: Shake It! (RWLE01) | Apr 2007 | 21 | 288 reads, 0 mismatches |
| Super Smash Bros. Brawl (RSBE01) | Dec 2007 apploader | 36 | 153 reads, 0 mismatches |
| Kirby's Epic Yarn (RK5E01) | Dec 2009 | 56 | **hung** (below) |

### 23.1 Kirby's Epic Yarn: no instruction BAT for MEM2

With the hook installed the game printed `Kernel built`, opened
`/dev/stm/immediate` and `/dev/stm/eventhook` and stopped; without the
hook it runs (230 reads). Nothing in the trampoline explained it (the
2009 prologue is `stwu / mflr / stw / addi r11` followed by `bl
_savegpr_23`, all replayable; the blob has no floating point, no
absolute addresses). Dolphin's PowerPC log at debug level did:

    W[PowerPC]: ISI exception at 0x935d0020

0x935d0020 is the blob's trampoline entry in MEM2. The first
`IOS_IoctlAsync` of the game (the STM event hook registration) reached
the stub, jumped to MEM2, and the fetch faulted: this SDK's start-up
leaves no instruction BAT covering MEM2 (data BATs stay, the game's own
MEM2 arena works). The 2007 and 2008 SDKs keep the BATs the loader set,
which is why sections 12-22 never saw it. Brainslug keeps its modules
at the top of MEM1 as well.

Fix: the blob (code and context, 7840 bytes) now goes to the top of the
MEM1 arena, the redirect table, MEM bytes and bounce buffers stay at the
top of the MEM2 arena and only when there are any
(`plan_resident_placement` plans both; `install_resident` performs
them). Where the MEM1 arena ends is the SDK's decision, read from the
game's `OSInit` (Kirby, 0x8066216c): `0x80003110` when non-zero, else
`0x80000034`, else the FST address. The loader used to mirror the FST
address into `0x80003110` ("MEM1 Arena End" on wiibrew); a first
attempt lowered only `0x34` and the game's arena still ended at the FST,
its first allocation from the arena top overwrote the blob and the
continuation jump went into the FST (`Program exception ... 0x817f3418`).
Both fields now hold the blob's base, computed from the apploader's
`0x34` (`0x38` when that is 0; `0x3110` is stale from the previous
program until the loader writes it). The apploader also parks the BI2
(`0x2000` bytes, pointer at `0x800000F4`) right below the FST, inside
the arena as the SDK sees it; the blob goes below the BI2 when it sits
there, so it stays whole for the debug-flag pointer `OSInit` keeps
into it. The MEM2 field `0x80003128` is left alone when nothing is
reserved there.

`Makefile.runtime` now compiles the blob with `-msoft-float`: the blob
was already free of floating-point instructions (checked by objdump);
the flag makes it a property of the build, not of the current source.
The binary is byte-identical.

### 23.2 Results with the MEM1 placement

| Title | Plain hook | Package |
| --- | --- | --- |
| Mario Kart Wii (RMCE01, Dec 2007 apploader, IOS36) | as before | the four test packages, all on: `M177ea3b6:00046d80:3b553c78`, `M17c3f6fc:00000e20:f6a09c08`, `M17c3f6f3:00000040:87a74ed3`, 7 memory patches, `Loader Type` printed; byte-identical to the MEM2 build's run |
| Super Smash Bros. Brawl | 153 reads, 0 mismatches | `brawl_test.xml` (grown `StrapEn.pac`, one created file): `M80000000:0002cf40:d88dcb79`, as in section 21's run |
| Wario Land: Shake It! | 288 reads, 0 mismatches | none written |
| Kirby's Epic Yarn | 195 reads, 0 mismatches; `MEM1 Arena : 0x809e4280 - 0x817e7fc0` | `kirby_test.xml`: `/param/fluff_param.txt` replaced in place from the card (185417 bytes, `M08c8d318:0002d460:af71e591`, the host's checksum of the file), `/homemenu/HomeButton2/home.csv` grown by one line into the virtual window (`M80000000:000011e0:19f77047`, host checksum of the file plus the zero gap), one created file (FST 82312 -> 82368 bytes). `M08c8875a:00012f00:58af4cb7` is the read of the file just before `fluff_param.txt`, rounded up by the DVD driver into its first 8 bytes: the checksum of `//RIFTWI`, the replacement's first 8 bytes, as it should be |

The Kirby package exercises, on a 2009-SDK title, the in-place SD path,
the virtual window, the FST rewrite with a created file and the
apploader overlay of the grown FST (loaded in one piece here).

### 23.3 What this says about hardware
The MEM1 placement removes a dependency on the loader's BATs surviving
the game's start-up, which no SDK promises; the MEM2 data placement
still relies on the game keeping a data BAT for the whole of MEM2, which
every SDK does because the game's own MEM2 arena lives there. The
assumptions of section 15.2 stand.

## 24. `<savegame>`: the plan (conductor, 2026-09-19)

### 24.1 What is documented
`<savegame external="/path" clone="true|false"/>` (patch-format wiki):
the title's save lives in that folder of the card instead of NAND;
`clone` (default true) copies the NAND save into the folder the first
time. Nothing else is specified, so the behaviour to reproduce is the
game's: every NAND operation it performs on its own data directory,
`/title/<type>/<id>/data/` (type `00010000` for disc titles, id the
four-character game id in hex), must work against the folder.

### 24.2 What the game does
The SDK's NAND library is a thin layer over ISFS, which is IPC: files
are `IOS_Open`ed by path and then `IOS_Read`/`IOS_Write`/`IOS_Seek`/
`IOS_Close`d by fd; everything else is an ioctl on the `/dev/fs` fd
with the path in the input buffer (0x09 CreateFile and 0x03 CreateDir
with an attribute block, 0x07 Delete, 0x08 Rename with two paths, 0x06
GetAttr, 0x05 SetAttr) or an ioctlv (0x04 ReadDir, 0x0C GetUsage), and
0x0B GetFileStats is an ioctl on the file's fd (wiibrew `/dev/fs`).
Each has a synchronous and an asynchronous form; games use both, and
the asynchronous chains run their next step from the IPC callback, so
the runtime side must be asynchronous as the DI path is.

### 24.3 The hook points
The SDK's IPC API is a cluster of 14 functions, an (async, sync) pair
per IPC command 1..7, each building the request block (`li rX,<cmd>`
stored to the block's first word), converting buffers to physical
addresses and calling one submit helper; the sync form passes a zero
callback and sleeps inside the helper on a thread queue in the block.
Kirby's Epic Yarn: Open 0x806a9210/0x806a9330, Close 0x806a9460/
0x806a9520, Read 0x806a95d0/0x806a96d0, Write 0x806a97e0/0x806a98e0,
Seek 0x806a99f0/0x806a9ad0, Ioctl 0x806a9bc0/0x806a9d00, Ioctlv
0x806a9f70/0x806aa060 (async first in every pair, as the two already
found by the DI and partition-open searches confirm). Hooking the
submit helper instead would mean completing requests the SDK's own
way (waking its thread queue, freeing its block): version-specific
internals, so the API functions are the hook points.

**The search (`find_ipc_api`, slice 3, done).** From the known
`IOS_IoctlAsync`: its block allocator is the `bl` preceded by
`li r4, 64; li r5, 32` (size, alignment) and its submit routine the
`bl` preceded by `mr r4, rX` (the callback). Every stack-frame
prologue within 0x2000 bytes either side that starts a function of at
most 0x400 bytes, calls both helpers and stores exactly one immediate
1..7 at the block's first word (`li rX, N` ... `stw rX, 0(rY)`) is an
API function; the argument to the submit call classifies it: a
register copy in r4 is the asynchronous form, `li r4, 0` the
synchronous one. Two functions of one class storing the same command
is an error. Left out: the reboot forms of ioctlv (a nonzero immediate
stored at block+0x28) and the IPC init function (stores 7 too, but is
816 bytes and passes no callback register). Functions the game never
calls are not in its DOL (the linker drops them: Wario Land has no
synchronous `IOS_Seek`) and stay 0; nothing calls them, so nothing
needs hooking. The known `IOS_IoctlAsync` must classify as async
ioctl and the known `IOS_IoctlvAsync` as async ioctlv, or the search
fails. `tools/ipcscan.cpp` runs it on a dumped `main.dol`; all four
titles give full pairs but Wario's sync Seek, and the synchronous
`IOS_Open` found agrees on all four with the function each SDK calls
for `/dev/stm/immediate` (a device-name cross-check outside the
search). The first attempt paired functions in address order and
insisted on two per command; Kirby's IPC init and Wario's missing
Seek broke that in one run each, hence the argument-based rule.

Each function gets the same trampoline as `IOS_IoctlAsync` (a template
instantiated 14 times, r3-r10 saved, one C dispatcher told which entry
it is). A sync hook may answer the caller directly (the trampoline's
"hijack" path); an async hook answers through the completion entry with
a pending record, exactly as DI reads do; SD I/O issued from a sync hook
uses the game's sync `IOS_Ioctlv` (the replay slot of its own hook),
from an async hook the async one.

### 24.4 The FAT32 engine
`runtime/rtfat.c`, freestanding C shared with the host tests like
`rtable.c`: a resumable engine. An operation is a record with a state
machine; `rtfat_step` runs it until it needs a sector transfer (it says
which device blocks, into or from the record's own sector buffer) or is
done with a result. A sync driver loops step / transfer; the async
driver issues the transfer as an SD request whose completion calls step
again. The same code serves both, and the host tests drive it against
an in-memory image with a fake device that counts transfers.

Volume parameters come from the loader (its `Fat32Volume` mount):
512-byte sectors only (what SD cards are formatted with; the loader
refuses others with a message), sectors per cluster, FAT location and
count, data start, cluster count, FSInfo sector, the save folder's
first cluster. Operations: lookup by name (8.3 and long names, case-
insensitive), read and write at a position with cluster allocation on
growth (both FATs updated, the directory entry's size after every
growing write), create (long-name entries with a generated short name
when the name is not 8.3), delete (entries freed, chain released),
rename, list, stats. One directory, flat: `CreateDir` is refused with
`-102`; ISFS error codes throughout (`-105` exists, `-106` not found,
`-108` no space, `-101` invalid).

### 24.5 The ISFS layer and the loader
`rtfs`: fake fds for the redirected files (a small table), the path
prefix to match (loader-filled), ISFS argument marshalling for each
command, ReadDir's 13-byte name slots, GetUsage from the listing, the
serialization of operations (one in flight; a sync arrival waits, an
async one queues) since IOS serializes too. The loader compiles
`<savegame>` into the context (prefix, volume, folder cluster), creates
the folder when missing, and allows the selection (`allow_savegames`).
`clone` is a second step: the loader cannot read another title's data
directory under IOS58, so the copy runs lazily in the runtime, as the
game, through the real `IOS_Open` (done: section 24.16).

### 24.6 Slices
1. `rtfat` with host tests (in-memory image, fake device). Done.
2. `rtfs` with host tests (fake IPC, ISFS argument blocks).
3. The 14-function search, validated on the four DOLs. Done.
4. Trampolines, dispatcher, pending records for FS operations.
5. Loader compile, Dolphin: Mario Kart Wii writes `rksys.dat` (2.5 MB)
   on first boot, Kirby's Epic Yarn a small save; the card image is
   checked on the host after the run. Mario Kart Wii done (24.13);
   Kirby open.

### 24.7 Slice 2 result (2026-09-19)
`runtime/rtfs.c` now adapts the documented 0x28-byte IOS request fields
and libogc ISFS payloads to the resumable FAT engine.  It keeps an
eight-entry generation-tagged fake-fd table, copies the loader-selected
data-directory prefix and FAT volume into its context, and marks each
request as pass-through, immediately complete, or needing device I/O.
Open, close, read, write, seek, CreateFile, Delete, Rename, GetAttr,
SetAttr, ReadDir, GetUsage, and GetFileStats are covered; CreateDir on
the redirected directory returns `-102`.  The adapter retains callback
and user-data words but neither invokes callbacks nor performs IOS/SD
I/O.  It allows one redirected request at a time and returns `-102` to
the second one.  GetAttr returns the fixed synthetic metadata owner 0,
group 0, attributes 0, and read/write (`3`) owner/group/other permissions;
SetAttr validates an existing redirected file and leaves its FAT data alone.

`rtfs_tests` drives the same state record one transfer at a time against
an in-memory FAT32 image.  It covers valid operations, pass-through and
malformed requests, mode and fd lifetime checks, seek bounds, IPC and
ISFS payload offsets, directory/usage vectors, transfer failure, and
the busy refusal.  The 2026-09-19 host configure/build/CTest command
passes all 15 suites under `-Werror`.

### 24.8 Slice 4A checkpoint (2026-09-19)
The resident blob ABI is now v3 and reserves deterministic wrapper,
replay, and continuation slots for all fourteen SDK IPC API forms:
async Open through Ioctlv at entries 0--6 and synchronous Open through
Ioctlv at entries 7--13.  The common dispatcher preserves the existing
async-Ioctl DI path and replays the other entries.  The loader still
installs only the legacy async-Ioctl hook; filesystem interception is not
enabled in this checkpoint.

### 24.9 Slice 4B1 checkpoint (2026-09-19)
`rt_build_fs_ipc` now translates each of the fourteen saved SDK IPC
argument forms into the common `rtfs_ipc` record without dereferencing
game memory or changing dispatch behavior.  This is translation only:
no filesystem hook is installed and no SD I/O is issued in 4B1.

### 24.10 Slice 4B2 checkpoint (2026-09-19)
(unchanged; see 24.11 for the busy-rule correction found while testing 4B3)

### 24.11 Slice 4B3 checkpoint (2026-09-19)
Async interception rides the same engine.  The blob ABI is v4 for one
addition: `complete_fs_offset`, a second completion entry shaped exactly
like the DI one, calling `rt_on_fs_complete`.  The loader parses v4 and
installs nothing new (no savegame context exists yet); host `TestBlob`
covers the field and its validation.

The dispatcher takes over async savegame calls the same speculative way
as sync ones (`rtfs_begin` classifies before mutating, so replays are
clean).  Transfer-needing requests claim the one FILE pend slot, swap
the callback pair for the FS completion entry and its tag, and hijack
with "accepted"; the host rig drives the transfers and invokes the
completion, which delivers the file result and the game's callback (the
console transfer-issue path is slice 5, so NEEDS_IO replays there when
no backend exists, exactly like 4B2).  Immediately-completing requests
are answered through the game's callback from inside the hook and
hijacked with the same result; a null callback just hijacks.  Async
`/dev/fs` opens replay to real IOS under observation (two snoop slots):
their completion learns the fd through `rtfs_learn_fs_fd`, and closing
it forgets it again (the engine owns that state; the dispatcher only
reads it).  Synchronous device opens stay unlearnable (their result is
invisible) and are documented as such.

Two corrections fell out of testing.  First, refusing a re-entrant
arrival inside `rtfs_begin` would zero the shared in-flight record, so
both dispatchers now refuse an already-busy engine before entering it,
delivering -102 to the new arrival's own callback and leaving the
in-flight operation intact.  Second, this slice builds on `b096d14`
(the concurrent slice-2 review: path-identified fd matching, probes,
malformed-passes-to-IOS), whose `rtfs_probe`/`rtfs_is_fs_device` the
dispatcher uses for the snoop compare; the 4B2 sync test's ISFS-ioctl
case was updated to the new engine semantics with it.
The seven synchronous entries now intercept savegame calls.  With
`RT_FLAG_FS` set and a loader-owned `rt_fs_state` block installed, the
dispatcher translates the call, runs `rtfs_begin` speculatively (every
begin path classifies before mutating, so a pass-through replay changes
nothing), hijacks immediately-completing requests with their result, and
drives transfer-needing ones through a backend hook to completion before
hijacking.  The state block lives outside the 2048-byte context (a
pointer plus a counter took two of its reserved words), so the blob ABI
stays v3.  Async entries still replay; ISFS ioctls on real fds replay
until 4B3 learns the `/dev/fs` fd.  A transfer-needing request replays
only when no transfer has run (the console has no backend in 4B2, and
the begin took the engine's busy flag, which is released first);
anything else anomalous hijacks an I/O error rather than replaying
half-applied state.  Host `hook_tests` drive open/read/write/seek/
close/stats through the dispatcher against an in-memory card image,
plus replay, gate, and busy cases.  Blob build needed two consequence
fixes: `rtfs.o` joined `Makefile.runtime`, and the blob provides its own
`memcpy` (PPC-only; the compiler lowers FAT-engine struct copies to it
even with `-fno-builtin`).  Blob 10304 to 26656 bytes; placement is by
`blob_size`, nothing hard-codes it.

### 24.12 Slice 4C: the savegame path completes in the runtime (conductor, 2026-09-19)
Review of 4B3 applied, then the missing pieces. Findings on 4B3 as
committed: a transfer-needing async call swapped the game's callback
in the register images and then hijacked, so the original never ran,
no transfer was issued and nothing could ever complete it on the
console (the host rig called the completion by hand); immediately
completing async calls invoked the game's callback from inside the
hook (IOS never does: code after `IOS_xxxAsync()` returns may set up
what the callback expects, and callbacks that issue the next request
would nest on the stack); a busy engine answered -102 instead of
serializing; and the sync `/dev/fs` open was declared unlearnable
although its replay slot is a callable function.

Now (`rt_hook.c`, "savegame requests"): one `rt_fs_issue` builds the
SENDCMD (CMD18 read / CMD25 write, the same vector shape libogc's
`wiisd.c` uses for both) in the state's own cache lines; the sync path
calls the game's synchronous `IOS_Ioctlv` through its replay slot
(`rt_fs_state.ioctlv_sync`), the async path its `IOS_IoctlvAsync` with
the FS completion entry and the FILE record as tag. The completion
sets the transfer status, steps the engine, issues the next transfer
or hands the game's callback to the tail call. Immediate completions
of async calls ride a null IOS round trip (SD GETSTATUS through the
unhooked async ioctl, DELIVER records) so the callback runs from the
IPC interrupt after the call returned; a direct call is the counted
last resort when no round trip can be issued. Serialization: async
arrivals behind a busy engine queue (4 deep, started from the
completion that frees the engine, deliveries in arrival order; a fifth
is refused at the call with -102 and no callback, as IOS refuses a
full queue); sync arrivals wait on the game's thread with interrupts
on, bounded by 10 s of the time base, then -114. Engine claims and
probes run with interrupts off (`rtfs_probe` from `b096d14`): the
game's IPC callbacks issue async calls, so hooks run in both
contexts. The sync `IOS_Open("/dev/fs")` is done for the game through
`rt_fs_state.open_sync` and the fd learned. An engine anomaly marks
the state dead (every later request -114, the card image untouched).
A completed read's bytes are written back to RAM (`dcbf`) in case the
game invalidates its buffer as after a DMA. Bounce 32 KiB. The context
layout is unchanged (it was exactly full: `rt_pending` is 480 bytes,
not the 448 its comment said); the two originals the FS path needs
live in the loader-owned state block.

Host: a fake IOS in `hook_tests` queues every request the runtime
issues and completes them oldest first through `rt_on_fs_complete`,
recording the tail-called deliveries, so the tests model the console
protocol: open/read/seek/write/close/missing-file through
completions, null callback, snoop, learned-fd close, queueing (order,
immediate ones deferred, full queue refused), a sync arrival waiting
out an async write, refused issue, failed transfer, inline last
resort, dead state, flag off. Blob 32832 bytes.

### 24.13 Slice 5: the loader, the title's data directory, imports; Mario Kart Wii saves to the card in Dolphin (conductor, 2026-09-19)
Loader (`wii/`): `<savegame external>` is planned (one folder per
launch, `clone` noted as not implemented: the loader cannot read
another title's NAND data under IOS58, section 24.5), the folder is
created on the card, the card unmounted and mounted again so libfat's
lazy writes reach it, and `resolve_sd_directory` (`sdfile.cpp`) turns
the folder into the engine's `rtfat_volume` (512-byte sectors only,
FSInfo's next-free cluster as the allocation hint). The data directory
is `/title/<hi>/<lo>/data` from the TMD's title id, not
`00010000-<game id>`: Mario Kart Wii is `00010004-524d4345`, a disc
title with a channel, and its first probe of the directory showed the
mistake. The installer hooks every SDK IPC function it finds
(`IOS_IoctlAsync` mandatory, the others skipped with a log line when
their first instructions cannot be displaced) and, for a savegame,
requires every present one hooked plus the card, the sync
`IOS_Ioctlv`, the async `IOS_Ioctlv` and the async `IOS_Ioctl`; the
state block (`rt_fs_state`, 70496 bytes) follows the bounce buffers in
the MEM2 data area, and the game's synchronous `IOS_Open`, `IOS_Close`,
`IOS_Read`, `IOS_Ioctl` and `IOS_Ioctlv` are recorded in it through
their replay slots.

Runtime findings from the first Mario Kart Wii runs, each fixed in
`rtfs.c` with a host test: `ReadDir` and `GetUsage` on a *file* name
are the SDK's existence probe, answered by a lookup: -101 when the
file exists, -106 when not (always -101 made the game think every
file existed). A rename onto an existing name replaces it, as IOS does
(delete, then rename: `RTFS_ACTION_RENAME_REPLACE`). And the SDK's
safe write: the game writes `/tmp/<file>` on NAND and then renames it
into the data directory (`banner.bin` when the save is created;
`rksys.dat` itself is created and written in place).
A rename across the directory's boundary is now an import
(`rt_hook.c`, `rt_fs_import`): the NAND file is opened and measured
through the game's own synchronous `IOS_Open` / `IOS_Ioctl
(GetFileStats)`, read in 32 KiB pieces into the state's import buffer
(flushed before each read, so no stale line shadows the DMA) and
written into a freshly created card file through the engine
(`rt_fs_run_internal`: the runtime's own requests take the engine like
the game's, waiting when it is busy), then closed and deleted from
NAND through the game's `/dev/fs` fd, which the rename also teaches
the adapter. A failure removes the half-written destination and leaves
the source. It runs on the game's thread: from the sync hook, or from
the async hook when `MSR[EE]` shows a thread rather than an IPC
callback; from a callback it is refused with -102 (none seen). A
rename out of the directory is refused with -102. Counters `imports`,
`import_failures`, `import_refused`.

Dolphin, Mario Kart Wii (`mkw_save.xml`, `sd:/riftwii/saves/mkw`):
the boot's probes of the directory, the `/tmp/banner.bin` import
(Dolphin's own IOS_FS log shows exactly `OpenFile`, `GetFileStatus`,
`Read 29344`, `Close`, `Delete` of the temporary and nothing at all
under `/title/00010004/524d4345/data`), `rksys.dat` created and
written in 0x2800-byte pieces through the fake fd, `wc24dl.vff` and
`wc24scr.vff` likewise; the game reaches its title flow (3122 disc
reads, 986 FS lines in 100 s). The card image afterwards holds
`BANNER.BIN` (29344 bytes, `WIBN`), `RKSYS.DAT` (2867200 bytes =
0x2BC000, `RKSD0006`, the header block's stored CRC32 `bff8f17a` equal
to the one computed over the bytes read back from the image),
`WC24DL.VFF` and `WC24SCR.VFF`. Host: `hook_tests` gained a fake NAND
(`FakeNand`) and the import drive (three-piece import, replacement,
outward refusal, missing source, mid-read failure with cleanup,
refusal without the sync originals, not-ours renames, async on a
thread with its 0 deferred, async from a callback refused);
`rtfs_tests` the existence probe, the replacing rename and the public
classifier. Blob 36928 bytes.

A second boot with that `rksys.dat` placed in the folder (the harness
rebuilds the card image from `WiiSDSync` at boot): the game finds it
(-101 from the existence probe), opens it, asks its stats, reads the
0x28000-byte header block in one read and closes; the VFFs, absent
again, are recreated, their attributes read and set (GetAttr/SetAttr
answered 0); 376 FS lines, 3253 disc reads, no error but the six -106
probes of files about to be created, nothing under the data directory
in Dolphin's IOS_FS log, no `/tmp` traffic (the banner is only made
with the save).

Kirby's Epic Yarn (`kirby_save.xml`, `sd:/riftwii/saves/kirby`, title
`00010000-524b3545`): a 2009 SDK that uses the asynchronous forms for
everything. Its boot opens `banner.bin` (-106 through the callback,
deferred by the null round trip), deletes `banner.bin`, `GF_0_00.jpg`
.. `GF_1_xx.jpg` and `FLF.bin` (each -106 the same way) and asks the
directory's usage (0), then waits at its first screen for a button
(200 disc reads, as its file-patch run in section 23); creating the
save needs input the harness does not supply, so that step is open.
With `kirby_test.xml` composed in front of it (two packages: the
in-place SD replacement, the grown file in the virtual window, the
created file, plus the redirect) the data area holds table, payload,
bounce buffers and the FS state together (327680 bytes at
0x93590000, state at 0x935c42e0) and both mechanisms answer as before
(section 23's three checksums, the same ISFS answers).
 Open: `clone`; Kirby's save creation with input; the hardware run of
 everything since section 11.

### 24.14 Snoop-slot claim locking (2026-09-19)
Review of the 4C runtime found one unguarded claim: the async
`/dev/fs`-open snoop slots were taken without interrupts off while
every sibling path (admit, deliver) locks. Two overlapping device
opens could take the same slot and lose a game's callback. The claim
now runs locked, mirroring `rt_fs_deliver`; host tests cover slot
exhaustion (two held snoops, a third replays unobserved, each
completion delivers to its own callback). The FILE pend and queue
claims need no change: no IOS completion can be outstanding across
their windows, so nothing can interleave them.


### 24.15 Kirby's save creation: the directory grows (conductor, 2026-09-19)
Driving Kirby's Epic Yarn past its first screen (a held `2` key in
Dolphin's window; a tapped key is too short for the emulated remote's
poll) reached "Creating save file": for `FLF.bin` and each
`GF_<n>_<nn>.jpg` the game opens (-106), asks the attributes (-106),
renames `/tmp/<file>` into the directory (the async form: the import
runs on the game's thread from the async hook, 0), opens the result,
asks its stats and closes it. The fifteenth import answered -107: the
save folder was one cluster of one sector (16 entries, `.` and `..`
included) and `rtfat` had no way to extend a directory. Now
(`9bef251`, then this commit): a creation that finds no run of free
entries goes past the end-of-directory sector, to the rest of its
cluster, to the next cluster already chained, or to a new one; the
entries from the stopped-at 0x00 onward are marked deleted first so
later scans read on (FAT: 0x00 ends the directory); a new cluster is
marked in every FAT copy, zeroed sector by sector, and only then
linked after the last one, so a failure between those steps costs a
lost cluster and never a directory tail of garbage entries; long
names never split across sectors. Rename shares the path. Host tests
fill the fixture's directory past its two clusters (chain, both FAT
copies, entry locations, the deleted marker, the engine's and the
host reader's listings) and inject a failure at every transfer of the
first growth, checking that a linked cluster is always zeroed.

Kirby again with the grown directory: the 32 imports (`FLF.bin`,
thirty `GF_<n>_<nn>.jpg`, `banner.bin`) all answer 0, the game reaches
its title, the file select reads each `GF_0_<nn>.jpg` back (async
open, one 0x20000-byte read, close) for its thumbnails, and a file can
be started into the intro; no FS request in the run answered an error
other than the -106 probes of files about to be made. The card image
afterwards: the folder's directory is a chain of three one-sector
clusters, `FLF.BIN` 43456 bytes (`FLUS`), thirty `GF_*.JPG` of 131072
bytes (`FLUS`), `BANNER.BIN` 61600 bytes (`WIBN`).

Reviewed and declined from another agent's pass over the same code
(kept aside, not committed): a rewrite of the async FS path that
scheduled every async request behind a null round trip and started it
from the IPC completion, dropped the queue (a second async arrival
answered -102), refused every async rename import (which is how Kirby
creates its save) and refused `/dev/fs` closes when the snoop slots
were busy; and a rollback "transaction" in `rtfat` that kept writing
FAT sectors after a failed transfer, against the engine's rule that a
failed transfer ends the operation and the dispatcher marks the state
dead. The ordering idea from that pass (zero before link) is what
section 24.15 keeps.

### 24.16 `<savegame clone>`; ReadDir packs names as IOS does (conductor, 2026-09-20)
`clone` (the attribute's default) is done in the runtime, as the game:
when the loader created the folder at this launch (an existing folder
is used as it is) and the selection asked for a clone, `rt_fs_state.
clone_pending` is set and the game's first hooked IOS call on a thread
(the sync hook, or the async hook with `MSR[EE]` on) runs `rt_fs_clone`
before it is answered: its own `/dev/fs` fd through the game's sync
`IOS_Open`, the data directory listed through the game's sync
`IOS_Ioctlv` (ReadDir, both forms), each name copied by `rt_fs_copy_in`
(the import's body, now shared: NAND read through the original
`IOS_Open`/`IOS_Read` in 32 KiB pieces, the card written through the
engine's internal requests, the source left in place), the fd closed
and not learned. A file that fails is skipped and counted; a missing
NAND directory is an empty save; without the sync originals the clone
is given up once. Gecko: `K:<path>:<result>` per file, the first line
the count. Loader: `SavegameOptions.clone`, `BootOptions.savegame_
clone`, `CompiledMod.savegame_clone` from the XML; the planner's note
says which. Blob 40864 bytes.

The first Dolphin run listed four names and copied one: the names
came back shifted (`c24dl.vff`, `ys.dat`, `.bin`). Dolphin's IOS
(`FileSystemProxy.cpp`, `ReadDirectory`) packs the names one after
another, each NUL-terminated (`address += size + 1`), in a buffer of
13 bytes per name, and libogc's callers walk them with `strlen + 1`;
the 13-byte slots `rtfat` wrote for the game's ReadDir were wrong the
same way in the other direction. Both sides now pack consecutively
(`rtfat` LIST: `list_bytes`; the clone's parser; the host fake NAND
packs as Dolphin does); the buffer of 13 bytes per name can never be
overrun since a name is at most 12 characters and a NUL.

Dolphin, Mario Kart Wii with `mkw_clone.xml` (`clone="true"`, a new
folder) and the NAND holding a save from an earlier unredirected run:
`K:` lines for the listing (4) and the four files (0 each); Dolphin's
IOS_FS log shows the two ReadDirectory calls and, per file, OpenFile,
GetFileStatus, the 32 KiB Reads and Close, no Delete; the game then
opens `rksys.dat` from the card directly (no creation), 3061 disc
reads. `RKSYS.DAT`, `BANNER.BIN` and `WC24DL.VFF` in the card image
are byte-identical to the NAND files (md5); `WC24SCR.VFF` differs
because the game rewrites two of its sectors on every boot, as in
every earlier run. Host: `hook_tests` `TestFsClone` (waits from an IPC
callback, runs on the first thread call, subdirectory skipped, NAND
untouched, own fd closed and not learned, once only, missing
directory, no sync ioctlv); the `rtfat`/`rtfs` listing tests check the
packing. Open: the hardware run of everything since section 11.

### 24.17 Review fixes before the hardware run (conductor, 2026-09-20)
A review of the savegame runtime and loader (`afa63ac..9b6f205`,
`docs/HANDOFF_2026-09-20.md`) found six issues; each is its own
commit, in this order.
1. `077a006`: a request completing on the game's thread (a sync call,
   an internal one of an import or clone) left the async queue
   standing until some later FILE completion; `rt_fs_start_queued` now
   runs after every thread-side completion. Test: an async open
   injected from a card transfer of a sync import is issued and
   delivered after the import.
2. `8e23a3a`: a sync arrival's wait spun with interrupts on and never
   yielded, so a holder of equal or higher priority could not run (the
   SDK schedules without time slices) and the waiter timed out after
   10 s. Each turn of the wait is now a synchronous SD GETSTATUS through
   the game's `IOS_Ioctl` (its own line, `wait_status`): the thread
   sleeps until IOS answers, which is after the holder's transfer since
   the SD device serves in order. Without that original the wait spins
   as before.
3. `7b71c53`: the async rename's import ran synchronously inside the
   game's `IOS_IoctlAsync` on the caller's thread (seconds for a
   multi-MB temporary) and could not run from an IPC callback. It is a
   job now (`rt_fs_job`, one at a time): the NAND side through the
   game's async originals (`IOS_OpenAsync`, `IOS_IoctlAsync`
   GetFileStats, `IOS_ReadAsync` per 32 KiB piece, `IOS_CloseAsync`,
   `IOS_IoctlAsync` Delete; the FS completion entry their callback,
   tag `&job.tag`), the card side through the engine's async FILE path
   (the pend's `job` flag returns the completion to the job; behind a
   busy engine the job's request queues like the game's, so the game's
   requests interleave and a sync arrival never waits out the whole
   file), the game's callback the tail call of the completion that
   ends the job. A failure closes and removes the destination and
   closes the source, which stays. The loader records the three async
   originals in the FS state (`IOS_IoctlAsync` is `di_read_entry`);
   without them the old behaviour stays (sync import on a thread, -102
   from a callback); the sync hook keeps the synchronous import. The F
   line says P at the call, the C line comes at the end. Host: the fake
   IOS completes NAND open/close/read/ioctl through the fake NAND; the
   import test drives a three-piece job from an IPC callback, one whose
   card request queues behind the game's own async open and whose NAND
   read fails midway (cleanup, -114 delivered), and both fallbacks.
4. `5bcbf7e`: every gecko line (F, C, K, DI R and M) is printed with
   interrupts off; thread and interrupt contexts shared the EXI channel
   and could cut each other's lines.
5. `447b08a`: a clone lists up to 512 names (was 128, the rest silently
   left out); every name past the cap counts in `clone_failures`, the
   K count line shows the directory's real count.
6. `5a3429c`: a clone ran only when the loader had just created the
   folder, so one that stopped short was never repeated. The loader
   now marks a folder whose clone is due with a hidden `riftwii.cln`
   inside it (libfat `FAT_setAttr`, `ATTR_HIDDEN`): created when a
   clone is decided (a new folder, or a marker still there), removed
   by the loader when the package says `clone="false"`, deleted by the
   runtime once the listing was copied to its end (an internal DELETE
   asking for hidden entries, `clone_marker`, a K line). `rtfat` skips
   hidden entries in lookups, listings and usage unless the operation's
   `want_hidden` asks (a creation always sees them: a name exists on
   the card once); `rtfs_ipc.hidden` carries the flag for internal
   requests. Found on the way: the count form of ReadDir (1 in, 1 out)
   always answered 0 through the engine (a LIST of length 0; a COUNT
   now), and the clone's engine-side requests now classify by the
   game's `/dev/fs` fd when it is already known (the clone's own fd
   would have passed through). Blob 45312 bytes.

Dolphin afterwards, Mario Kart Wii: `mkw_save.xml` on the existing
folder (rksys.dat read back, the VFFs created and written, GetUsage's
count form answered, only -106/-101 probes as errors) and
`mkw_clone.xml` on a new folder: the loader's marker is in the
directory (`RIFTWII CLN`, attribute 0x22), the clone lists 4 and
copies 4 (`K:` lines), the fifth K line is the marker's delete (0),
the card image afterwards holds the marker's entry as deleted (0xE5)
and `RKSYS.DAT`, `BANNER.BIN`, `WC24DL.VFF` byte-identical to NAND
(md5). Kirby's Epic Yarn boots as in 24.15 (33 async deletes, -106
each, through the job-free path; usage 0) and waits at its first
screen; its save creation, which would drive the import job with
thirty-two real async renames, needs the held `2` key, and the harness
could not take the keyboard this time (another window held the
foreground, and screenshots came back stale), so that run is open
along with the hardware run of everything since section 11. Note for
the harness: a killed run's `run.sh` keeps sleeping and its final
`taskkill` ends whatever Dolphin runs next; wait for it or kill the
script too.

### 24.18 Handoff safeguards (conductor, 2026-09-20)
Before the game entry is called, the loader owns an open, selected raw SD
card only while it is preparing the resident. A scope guard now deselects
and closes it on every failed preparation path; ownership transfers only
immediately before the irreversible handoff, and an unexpected return from
the game entry closes it too. Memory patch planning now removes the full
16-byte range of every IPC entry actually overwritten by a resident
trampoline, rather than protecting only `IOS_IoctlAsync`. The range helper
is host tested with unsorted, duplicate, touching, overlapping and
out-of-range exclusions, and with ordinary writable gaps on either side of
a protected stub. A clone marker that already exists is also hidden and
verified again before launch, so a previous failed attribute write cannot
leave it visible to the game.

### 24.19 Directory-extension recovery (conductor, 2026-09-20)

`rtfat` now treats a directory extension as a recoverable card mutation.
It marks the candidate, zeroes every candidate sector, and only then links
the old directory tail. If any forward request fails before the first new
dirent is durable, it restores that tail to EOC in every FAT copy and then
clears the candidate in every copy. The sweep keeps attempting later copies
after a recovery I/O failure. If that sweep cannot prove the mirrored FATs
consistent, the volume is marked mutation-uncertain: later writes, creates,
deletes and renames answer `-114` until a remount; read-only lookups and
reads remain available. FSInfo is deliberately not updated: its next-free
field is advisory, while `alloc_hint` is reset so it cannot skip the
restored free candidate. Host fault injection fails each transfer through a
growth, including candidate mark/zero/link and the first dirent; every
one-shot failure restores byte-identical directory/FAT state and preserved
files. A second failure during rollback poisons the volume and rejects a
later mutation. This protects request failures, not sudden power loss while
the card writes sectors: FAT32 has no journal, so hardware power-loss tests
remain required.

The same conservative stop applies to ordinary file-chain allocation and
freeing. Those mutations have no rollback log, so a failed FAT-copy write
immediately marks the volume uncertain rather than allowing a later save
write to compound a possible mirror mismatch. Focused host injection covers
every FAT write in a growing file and in deletion, then verifies that a
later create is refused while a lookup still works.

### 24.21 Kirby's async import job in Dolphin (2026-09-20)
The last open Dolphin item from 24.17 (thirty-two real async renames
through the import job) is done. Fresh boot, `kirby_save.xml`, input
from a hand-built DTM movie: the Yes/No save prompt confirmed, the
game reached its title, and the card holds `FLF.BIN` (43456, `FLUS`),
thirty `GF_*.JPG` (131072 each, `FLUS`) and `BANNER.BIN` (61600,
`WIBN`), sizes and magics exactly as in 24.15. Dolphin's IOS_FS log
shows all 32 NAND `/tmp` sources created, read in pieces, closed and
deleted. Kirby issues its renames through async IOS, and the loader
had recorded the async originals, so these 32 went through the
asynchronous job path (`import_jobs`), not the sync fallback. Host
`TestFsJobKirby` drives the same 32; it now also asserts no stage
survives, as do the sync import and clone tests (`ExpectNoStage`,
through the independent host FAT reader, which sees hidden entries).

Input notes for the harness, all learned the hard way: Dolphin in
`-b` batch mode never shows its window, so nothing can take focus and
no live input (keys, clicks, hotkeys) can arrive; a DTM movie needs
the booted DOL's game ID (`ID-riftwii`, first six `ID-rif`), not the
game's, or playback aborts on the mismatch warning; the working
formula is 600 empty frames then a rotating 2-tap, A-tap, Down-tap
cycle so every prompt gets fresh edges. The Gecko TCP server never
listened in movie runs (reason unknown, secondary: screenshots plus
the card image carry the evidence).

One anomaly on the card: a live hidden `.rwstage.tmp` shares
`BANNER.BIN`'s chain (same first cluster, both 61600 bytes). The game
visible state is perfect (32/32 files, title reached), and the engine
side is clean: the rename/delete path was re-reviewed, a host repro
of the exact commit shape (`TestCommitGhost`: 30 hidden-stage
create-write-rename cycles with directory growth, chains asserted
exclusive, host reader agreeing) passes, and 31 of the 32 same-path
operations left nothing. Leading hypothesis is the harness, not the
engine: Dolphin was killed with taskkill instead of a clean shutdown,
and a lazily flushed directory sector (the entry write landed, the
later mark write did not) explains a live stage beside a live banner
exactly. Harness rule from now on: shut Dolphin down cleanly (or let
the autorun power off) before reading the card image. Recorded as a
known issue in the prerelease notes. If the stage ever recurs after a
clean shutdown, the next step is a verify-after-rename in the commit
path (re-lookup the stage name; fail loud instead of leaving a
cross-link), because a later stale-delete would free the shared chain
under the live file.

### 24.20 Takeover of the in-flight tree (2026-09-20)
Opus's 24.19 tree was taken over mid-flight (hook tests red, blob
`.rodata` failing the position-independence check). Three fixes, each
verified by the host suite (15/15), the runtime blob build and the Wii
DOL build:
1. The synchronous import's stage/backup/rename path buffers lived on
the C stack, whose 64-bit host addresses truncate through the engine's
32-bit fields; the first internal DELETE crashed in `path_type` on a
truncated pointer. They now live in `rt_fs_state` (`copy_stage`,
`copy_backup`, `copy_paths`: one sync import runs at a time behind the
engine's one-at-a-time rule) like the job's record-held paths.
2. The same rework's `".rwstage.tmp"` / `".rwback.tmp"` literals put 28
bytes in `.rodata`, failing the blob's no-rodata rule. They are now
spelled out char by char (`rt_stage_name`, `rt_backup_name`), PPC and
host alike.
3. The async rename's refused/import-failed path answered through the
call (`*result = r`) while every sibling async answer is accept-at-call
plus callback delivery, and a failed `rt_fs_deliver` (no null round
trip possible) fell back to inline invocation, which 24.12 had removed
for ordering reasons. Refusals now go through `rt_fs_deliver` and the
call reports 0, or `-114` when no delivery can be scheduled; the host
tests were updated to the fail-closed semantics (no inline callbacks).
The snoop/open/close async tests were also updated to the explicit-
original-call dispatcher (they still asserted the old register-swap
shape), with the async original fakes wired in.

## USB d2x milestone

The USB source is a FAT32-only, read-only catalog. Its host-tested mapper turns
ISO/WBFS file extents into d2x F9 fragments, including sparse WBFS holes. The
GUI validates only the container identity; after it exits, the launcher reloads
d2x, configures F9, disables DI reset, remounts SD, then probes and compiles
through virtual `/dev/di`. `BootOptions::preserve_current_ios` keeps d2x while
the existing apploader, resident redirect, and save runtime run unchanged.
See `docs/USB_HARDWARE_TEST.md` for the required hardware evidence.

### USB follow-up (2026-09-21)
Two defects that would have stopped real images, both fixed and host-tested:
- FileByteSource's 256 MiB cap rejected every real game image (and stat()
  cannot size multi-GB files on 32-bit targets at all). The catalog now
  opens pieces with the FAT32 entry size via a new uncapped open; only
  small header reads go through it, bulk bytes stay with d2x. Covered by
  3/6 GiB ISO and 4 GiB WBFS tests plus a plan-over-USB-source composition
  test (XML to consumed replacement bytes).
- Split-piece discovery did up to ~1000 FAT lookups per game (the gap
  lookahead), which would stall the GUI on hardware. It now collects from
  the in-memory directory listing (`collect_split_pieces`, host-tested
  including gap and case handling).
Still hardware-only: d2x load/F9 DMA/F6 behavior, sector geometry, and a
full USB game-plus-mods boot.

### cIOS readiness warnings (2026-09-21)
A version-number check is not possible on device: the guided installer
stamps revision 65535 for every d2x version, and tool-reported version
labels come from heuristics over leftover install artefacts. So the
loader checks what is checkable: at scan time it queries cIOS tickets
for 249/250/251 without reloading IOS and warns in the GUI status line
when none is installed (skipped under Dolphin, which has no slots); at
launch it logs the reloaded IOS version and revision, rejects the
well-known stub marker 65280, and names d2x v11 beta3 in the probe
failure. Any d2x passing the F9/FA probe supports everything the loader
uses, so no older-version warning is emitted.
