# Plugins: a design

Status: stage (a) is built (the loader, API 1, the `selftest` plugin, the
packaging and the checks; "Stage (a) as built" below). Stage (b), the
GameCube loader as the `gamecube` plugin, is built too ("Stage (b): the
GameCube loader as a plugin" has its design, "Stage (b) as built" what
came out and the sizes); stage (c) is still the design. No number here was measured on a Wii; the sizes
of stage (a) are from the Wii build, the rest are estimates. The words
"must" and "will" describe the design.

## Stage (a) as built

| Piece | Where |
| --- | --- |
| The tables, API 1 | `include/riftwii/plugin_api.h`: `rwp_host` (log; arena, MEM1 and cache; file read and size; version and build stamp; crash ranges) and `rwp_plugin` (probe, shutdown). Both start with their size and the API number; the Menu, Launch and System groups are left to stage (b), at the end |
| The file | `include/riftwii/pluginfile.hpp` has the header's byte layout; `src/data/pluginfile.cpp` the checks, the relocations and the file names; `tests/plugin_tests.cpp` covers them |
| The build | `Makefile.plugin` (run by `Makefile.wii` before the DOL, with its version and stamp), `plugins/plugin.ld`, `tools/make_plugin.py` (two-base check, relocation list, header and SHA-256; `check` for the version match, `variant` for refusal tests) |
| The loader | `wii/core/plugins.cpp`: on first use, from the running DOL's folder |
| Crash attribution | `wii/core/crash.cpp`: `PC is in plugin selftest +0x...`, the same for LR and each stack address, and the loaded plugins with their stamps; the restart note carries the PC's line; problem reports list the run's plugins |
| The test plugin | `plugins/selftest/selftest.c`, loaded only by `sd:/riftwii/plugin_test.txt` ([DEVELOPING.md](DEVELOPING.md#the-plugin-test-switch)) |
| Packaging | `apps/riftwii/plugins/` in the zip and the update pack's apps part (`AppsFileAllowed`), checked against the zip's DOL by `tools/make_update_pack.py`; CI's stamp check and size rows |

Decided while building it, against the open questions of section 9:

- **Location:** `sd:/apps/riftwii/plugins/`, next to the running
  `boot.dol` (`argv[0]`'s folder, else `sd:/apps/riftwii/plugins/`, or
  `plugins = <folder>` in `settings.txt` for tests).
- **Names:** stamped with the version, `<name>.<version>.rwp`; the header
  carries the full build stamp, the version and the commit
  (`2610-469-ga5ef290f`), and both are checked. A file of another version
  is never opened: when it is the only one there, the refusal says the
  plugin is from a different RiftWii and names both versions. Once the
  update's apps part is written, the other versions' files are removed.
- **Format:** an image with a relocation list for every plugin (a pure
  position-independent plugin simply has none).
- **Strings:** the refusal sentences are the loader's, in the DOL's
  tables. `tools/lang_source.py` also scans `plugins/` for `tr()` text.

Measured: the loader costs 26 KiB of MEM1 (the program's end moves from
`0x810D4840` to `0x810DB0C0`; `plugins.o` 14 KiB, `pluginfile.o` 7 KiB,
`sha256.o` 2 KiB, built with `-Os`), the saving comes with stage (b).
`selftest` is 2,892 bytes on the card and takes 2,016 bytes of the
arena (image 1,984, `.bss` 8, 65 relocations).

Still to run in Dolphin for stage (a), beyond the runs in the pull
request:

- **The arena that stays taken.** `ReleaseArena` gives the 256 KiB back
  only when arena 2's high end is still where the loader left it
  (`SYS_GetArena2Hi()` equal to the arena's low end); when something has
  moved it since, the log says `the arena at 0x... stays taken (MEM2's
  top moved since)` and the memory is lost to the heap until RiftWii
  restarts. No code in RiftWii is known to move it, so the test is to
  find out whether any real path does: load `selftest` by the test
  switch, then go through what can touch MEM2's top before "the menu
  closed" (a Wii launch, a GameCube launch from a disc, an ISO and an
  RVZ, a failed launch coming back, a theme and a language restart, an
  IOS reload, the in-app update, the network on) and search each
  `session.log` for `stays taken`. Every run should say `is the heap's
  again`; a run that does not names the path to fix.

What the in-app update did in stage (a): it wrote the new `boot.dol`, and
the next start wrote the release's apps part (plugins included) from
`riftwii-update.pack`. Between the two, the new DOL found no file of its
own version and said the add-on was missing; it never ran an old one.
Stage (b) makes the update write the plugins before the DOL ("Updates:
plugins first" below), so the GameCube loader is there from the first
start.

## Stage (b): the GameCube loader as a plugin

The maintainer's choice for the boundary (pull request #121, option 2): a
C interface, a table of plain C function pointers, with the C++ kept
inside the plugin. A throwaway link of the loader against `plugin.ld`
found 99 undefined symbols (117 after #113, #115 and #116): libstdc++'s
out-of-line parts, the C library, libogc, Zstandard, four embedded blobs
and 24 of RiftWii's own functions that take `std::string`, `std::vector`
or `Settings()`. None of them may cross as C++. This section is the design
for moving it; "Stage (b) as built" below records what came out.

### What moves

The plugin is `gamecube` (`plugins/gamecube/`). Its one job is what
`LaunchGcNative` did: read the game's DOL and FST, find and hook its
functions, lay out MEM1 and MEM2, hand the SD card over and jump. Into
it go:

- `wii/launch/gcnative.cpp`, moved to `plugins/gamecube/gcnative.cpp`;
- the GameCube runtime blobs, which only it copies into a game:
  `riftwii_gc.bin`, `riftwii_gc_rvz.zst` and `riftwii_gc_rvz.rel`;
- its own copies of the platform-free code it calls from `src/`
  (`gcclock`, `gcreset`, `gclayout`, `gccontrols`, `gcvideo`, `gcoverlay`,
  `rvz`, `sha1`, `symsearch`, `codehook`, `hook`, `redirect`, the game
  lists of `gxpatches`), linked with `--gc-sections` so only what it reaches
  is kept. The ones nothing else in the DOL uses (`gcclock`, `gcreset`,
  `gclayout`, `gccontrols`) leave the DOL; the others stay in both, as
  section 4 allows ("linked into the plugin that needs them").

What stays in the DOL: listing the games, their pages, their options and
the packs' plan (`gclaunch.cpp`), the memory card backup, the crash
screen the menu draws in its font, and the way back (the ES ticket views):
`wii/launch/gcplugin.cpp` keeps `FindGcWayBack` and `SetGcCrashScreen`,
and `LaunchGcNative` there becomes the call into the plugin.

### The plugin's table: launch

`rwp_plugin` gains one entry, after `shutdown`:

    int32_t (*launch)(const void* request, uint32_t request_bytes);

It starts what the plugin starts and returns only on failure. The request
is the plugin's own C structure, here `rwp_gc_launch` in
`include/riftwii/plugin_gamecube.h`, which begins with its size: the game
(path, ID, whether it is an RVZ, the game's options as numbers), the packs'
plan as flat arrays (the new FST, the header bytes, the window's entries),
a snapshot of what the launch reads from the settings and the launch's
extras (rumble off, Dolphin or a Wii, the cheats' code list and count,
whether a code build's own codes were picked, the code handler's bytes),
the crash screen's bytes and whether its hint promises a way back, the way
back (its title, kind and ticket view, fetched by the DOL), and an error
buffer. The plugin turns the arrays back into `GcImage` and
`GcOverlayPlan` and runs the moved code. `kPluginTableBytes` becomes 28,
and `plugin_check_table` checks `launch` as it does `probe`: NULL or a
word of code inside the image.

### The host table: API 2

RiftWii's table grows at its end; `RWP_API_VERSION` becomes 2, as
`rwp_plugin` changed too. Each new entry is a plain C function with C
types (pointers and sizes, out-structures with fixed fields), grouped by
what the loader needs:

| Group | Entries | Replaces |
| --- | --- | --- |
| C runtime | `format` (vsnprintf), `move_bytes` (memmove), `fill_bytes` (memset) | `snprintf`, `memcpy`, `memmove`, `memset` |
| Log | `log_text` (a line as it is, its own "GameCube: " start and line end), `log_close` | `logf`, `LogClose` |
| Files | `stream_open`, `stream_close`, `stream_read`, `stream_write`, `stream_seek`, `stream_tell` (an opaque handle, 64-bit offsets), `path_info` (size, folder or file), `make_directory` | `fopen` and the rest, `stat`, `mkdir` |
| The SD card, raw | `sd_file_fragments` (a file's sectors as `rwp_fragment` pairs and its size), `sd_forget_layout`, `sd_hand_over` (unmount, shut the interface down, open the card for the game: `rwp_sd_card` with its fd, RCA and SDHC flag) | `resolve_sd_file`, `forget_sd_layout`, `fatUnmount`, `sd_interface()->shutdown()`, `sdio::open_card` |
| System | `ios_open`, `read_sram` (`rwp_sram`: the clock bias, the language, slot A's flash ID), `video_framebuffer`, `shut_down_for_game` (the TV mode as a number, `RWP_TV_NTSC` or `RWP_TV_PAL`; configures the video encoder, two retraces, `SYS_ResetSystem(SYS_SHUTDOWN)`) | `IOS_Open`, `__SYS_LockSram*`, `VIDEO_*` with `TVNtsc480IntDf`/`TVPal528IntDf`, `SYS_ResetSystem` |
| Zstandard | `zstd_decompress`, `zstd_frame_size`, `zstd_is_error` | the three `ZSTD_*` the RVZ reader and the RVZ blob need |
| The USB drive, after API 2 | `usb_read` (512-byte sectors through d2x's `/dev/usb2`, once a GameCube game's launch has opened it; checked with `RWP_HOST_HAS`) | `ums::Read` |

The cache entries of API 1 (`flush_range`, `sync_code`) replace
`DCFlushRange` and `ICInvalidateRange`, and `mem1_alloc` and `mem1_free`
(the loader's heap, which carries on into MEM2) back the plugin's
`operator new`. Not in the table, because the DOL does them before the
call: the ES ticket views of the way back, `Settings()`, the launch's
extras, the memory card backup (`gcbackup`, which the menu uses too) and
whether the image is an RVZ.

### C++ inside the plugin

Built with `-fno-exceptions -fno-rtti -fno-threadsafe-statics`.
`plugins/common/rwpruntime.cpp`, linked into every C++ plugin, gives the
link what libstdc++ and the C library would: `operator new` and `delete`
on `mem1_alloc` and `mem1_free`, `memcpy`, `memmove`, `memset`, `memcmp`,
`strlen`, the stdio calls and `stat` and `mkdir` over the Files group,
`snprintf` over `format`, the `ZSTD_*` names over the Zstandard group,
`abort`, `__cxa_pure_virtual`, and the `std::__throw_*` functions.
`std::string`'s members are instantiated in the plugin itself (an explicit
instantiation in `rwpruntime.cpp`, compiled without exceptions), so
nothing of libstdc++'s exception-built objects is linked. The one
`dynamic_cast` (the RVZ reader's) goes: the plugin opens the disc itself,
so it keeps the RVZ source it made.

Nothing may throw, so the `__throw_*` functions and an `operator new`
that finds no memory do not trap: during a launch they jump back to the
plugin's `launch` (`__builtin_setjmp`, no library needed) and it returns
the failure. Out of memory comes back as its own result, and the DOL
throws `std::bad_alloc` there, so the existing "RiftWii ran out of memory
while starting the game" message (`mem::OutOfMemoryAsError`) still shows.
What the launch had allocated is not freed: the failure path restarts
RiftWii anyway (`OfferRestart`). Outside a launch (its `init`) such a
failure stops the plugin with a trap, which the crash report names.

The one global with a constructor, the crash screen's `std::vector`,
leaves the plugin: the DOL keeps the screen and passes its bytes in the
request.

### Where the plugin runs, and staying out of the game's way

The plugin outlives the menu: it is loaded at the launch, after "the menu
closed" (the menu's `ShutdownAll` runs before any launch), and its code
runs to the jump. Its arena is the top of arena 2, above everything a
GameCube launch writes in MEM2 (the card and buffers from `0x91000000`,
the Reset snapshot to `0x92D00000`; MEMORY.md). The plugin checks this
before it writes anything: when its own image is within one arena's size
of that range, the launch stops with a sentence saying so instead of
overwriting its own code. Everything it calls after `shut_down_for_game`
(the cache functions, `move_bytes`, `fill_bytes`) is DOL code above
`0x80A00000`, which no game section reaches (`read_dol` refuses one that
does).

### When the plugin is missing

`LaunchGcNative` (DOL) asks `plugin::Launch("gamecube", ...)`, which loads
the plugin on first use. When it cannot be used (missing, damaged, from
another RiftWii, no memory), the launch fails with the loader's sentence
on screen (translated, "A part of RiftWii is missing (the gamecube
add-on). Update RiftWii again.") and the log says
`GameCube games need RiftWii's gamecube plugin in <folder> (<file>): ...`
with the refusal's reason. RiftWii comes back as for any failed launch.

### Updates: plugins first

The in-app update knows the release's `riftwii-update.pack` from the
update check (its URL, size and SHA-256 kept in the update note with the
DOL's). It downloads the pack with the DOL, checks both, and writes the
pack's plugin files (`riftwii/plugins/<name>.<version>.rwp`, the new
version's names) next to the running DOL before it writes `boot.dol`; a
plugin it cannot write stops the update before the DOL is touched. The
stamped names keep the old DOL working until the new one is in place,
and the new DOL's first start finds its own plugins. The rest of the apps
part and the themes are written at that start, as before, which also
removes the old version's plugins. The order is planned by
`plan_update_writes` (`src/net/update.cpp`, host-tested).
`tools/make_update_pack.py` puts the plugins first in the apps part and
fails a zip without the `gamecube` plugin.

### Tests

Host: the table's size and packing (`plugin_tests`), the request's layout
and the round trip of a packs' plan through it (`gcrequest_tests`), the
update's order. Build: `make_plugin.py check` for `selftest` and
`gamecube`, the size report. Dolphin: an ISO and an RVZ started by
`autorun.txt` with the plugin there, and with it missing; the arena's
"stays taken" check in each `session.log`.

## Stage (b) as built

| Piece | Where |
| --- | --- |
| The tables, API 2 | `include/riftwii/plugin_api.h`: API 1's twelve functions and 24 more (the groups above, plus `zstd_error_name`, which the RVZ reader's messages use), 36 in all, then `usb_read` at the end (37); `rwp_plugin` with `launch` (28 bytes, `kPluginTableBytes`) |
| The request | `include/riftwii/plugin_gamecube.h` (`rwp_gc_launch`), `src/patching/gcrequest.cpp` (the packs' plan flattened and built again, the plugin's check of a request; `tests/gcrequest_tests.cpp`) |
| The DOL's side | `wii/launch/gcplugin.cpp`: `LaunchGcNative` fills the request and calls `plugin::Launch("gamecube", ...)`; `FindGcWayBack`, `SetGcCrashScreen` and the card's backup stay here; `wii/core/plugins.cpp` has the host functions and `Launch` |
| The plugin | `plugins/gamecube/`: `gcnative.cpp` (the loader, moved), `gcentry.cpp` (`rwp_init`, the table, `launch`), `gchost.cpp` (the loader's helpers on the table: `logf`, `resolve_sd_file`, the disc opened as an ISO or an RVZ, the arena check), `gcblobs.S` (the three `runtime/gc` blobs by `.incbin`) |
| A C++ plugin's runtime | `plugins/common/rwpruntime.cpp`: `operator new` and `delete`, `memcpy`, `memmove`, `memset`, `memcmp`, `memchr`, `strlen`, `snprintf`, the stdio calls, `stat`, `mkdir`, the `ZSTD_*` names (a decoding context stands for "make one per call"), `abort`, `__cxa_pure_virtual` and the `std::__throw_*` functions; `std::string` instantiated here |
| The build | `Makefile.plugin`: `.cpp` and `.S` sources, `plugins/common/` for C++ plugins, `<name>_SRC` for the `src/` files a plugin links its own copy of, and libstdc++'s `tree.o` (the red-black tree's balancing, for `std::map`) taken out of the library alone with its unwinding table removed |
| Out of the DOL | `Makefile.wii`: `gclayout`, `gcclock`, `gcreset` and `gccontrols` in `EXCLUDES`, the `riftwii_gc*` blobs out of `BINFILES` |
| Updates | `plan_update_writes` (`src/net/update.cpp`, `tests/plugin_tests.cpp`), `WriteUpdatePlugins` (`wii/online/online.cpp`, the pack's asset kept in the update note), `tools/make_update_pack.py` (plugins first, the `gamecube` plugin required) |

Changed from the design while building it:

- `place_on_fragments` moved out of `src/patching/redirect.cpp` into
  `src/fs/fragments.cpp`: the redirect table's builder beside it catches
  `std::bad_alloc`, which cannot be built without exceptions, and the
  plugin needs only the one function.
- `src/patching/launch.cpp` (for `gc_pal_game`) and `runtime/rtrvz.c`
  (the RVZ reader's unpacking) are linked into the plugin too.
- `std::make_shared` is not used in the plugin: libstdc++ asks RTTI for
  its tag. The disc's `shared_ptr` is made from `new`.
- libogc's headers stay out of the moved code except for its types and
  `_CPU_ISR_Disable`: `gettime` is the plugin's own (two reads of the time
  base), and `DCFlushRange` and `ICInvalidateRange` are names in
  `gchost.hpp` over `flush_range` and `sync_code`.

Measured (Wii build of this branch, against the merge of upstream main it
starts from):

| | Before | After |
| --- | ---: | ---: |
| `boot.dol` | 7,091,712 bytes | 6,890,752 bytes |
| The program's end in MEM1 (`__bss_end`) | `0x810F40A0` | `0x810C2FC0` |
| MEM1 back to the heap at every start | | 200,928 bytes (196 KiB) |
| `gamecube.<version>.rwp` on the card | | 192,328 bytes |
| Its share of the 256 KiB arena | | 180,000 bytes (image 179,936, `.bss` 40, 1,022 relocations) |

Of the plugin's image, 75,106 bytes are the three blobs (`riftwii_gc.bin`
31,488, `riftwii_gc_rvz.zst` 40,822, `riftwii_gc_rvz.rel` 2,796), 90 KiB
the loader's code and its copies of `src/`. The DOL's saving is net of
what stage (b) added to it (the host table's 24 functions, the request,
the update's plugin step).

Still to run in Dolphin for stage (b): a GameCube ISO and an RVZ started
by `autorun.txt` with the plugin there (the log has `Plugins: gamecube 1
read from ...`, its arena, `Plugin gamecube: version 1 ready` and then the
loader's `GameCube: ...` lines as before), the same with the plugin
missing (the launch fails with the add-on sentence and the log line
`GameCube games need RiftWii's gamecube plugin in ...`, and RiftWii comes
back), and the arena check of stage (a) for each (`is the heap's again`).
Only a Wii shows the timing, the SD card's hand-over and the d2x cases.

RiftWii's loader (`boot.dol`) is linked at `0x80A00000` and grows upward
toward the apploader at `0x81200000`; its heap is the space between its
image end and that address ([ARCHITECTURE.md](ARCHITECTURE.md#the-loaders-size-and-the-games-room)).
The Homebrew Channel loads the whole DOL into RAM and the Wii does not page,
so every feature RiftWii has costs MEM1 whether or not a player ever uses
it. The start-up stash ([MEMORY.md](MEMORY.md)) already moves the data
only needed now and then (the menu font, translations, GPL) to MEM2. This
document is about the other half: code that most launches never run can
stay on the card until it is needed.

## 1. Goals and non-goals

Goals:

- **MEM1 relief.** The code and read-only data of a feature that is not in
  use leave `boot.dol` and so leave the MEM1 image.
- **Optional features loaded on demand.** A plugin is read from the card
  the first time its feature is used, into MEM2, and can be dropped again.
- **One bundle.** Plugins ship inside the release zip and
  `riftwii-update.pack`, and are written together with `boot.dol`. A player
  installs and updates RiftWii once; there is nothing else to find, trust
  or keep in step.

Non-goals:

- Third-party plugins, a plugin store, or any download of a plugin from a
  network. The maintainer's constraint is that everything stays bundled.
- Running untrusted code. A plugin runs with the loader's full rights, in
  the same address space, exactly like the rest of RiftWii. Nothing in this
  design sandboxes it, and nothing will claim to. Because only RiftWii's
  own build produces plugins and the update pack's SHA-256 already guards
  what is written, no signature scheme is proposed either (section 7).
- A stable public API. The function table is versioned (section 4) but is
  private to the project: it changes whenever RiftWii needs it to, and a
  plugin of another build is simply refused.
- Replacing the in-game blobs under `runtime/`. Those run inside a game,
  already are position independent and are installed by their own hooks;
  this design reuses their build rules but does not move them.
- Saving MEM1 inside a running game. Plugins relieve the *loader*; the
  game's own MEM1 is a separate matter.

## 2. Format

A plugin is one file, `<name>.rwp`, built like the runtime blobs: freestanding
C or restricted C++ for the 750CL, no libc and no heap of its own, linked
twice at two different bases and refused if the two images differ in
anything but a relocation. `Makefile.runtime` already does this for the
resident runtime, and `tools/rtreloc.py` already writes a relocation list
for the two blobs that carry constant tables (the RVZ runtime and the
GameCube RVZ blob), applied by the loader that copies them. The plugin
format uses both mechanisms:

- **Pure position-independent code** where the plugin is small and plain
  (the test plugin, a launcher).
- **An image plus a relocation list** where the code has tables of
  pointers (the GameCube loader, which is C++ with `std::` containers from
  `src/`, has plenty). The loader adds the load address to each listed word.
  This is the existing RVZ-blob approach, not a new loader.

Allowed in a plugin: code, read-only data, zero-initialised `.bss` (its size
is in the header and the loader clears it), and writable data only inside
a state structure the plugin asks for. Not allowed: static constructors and
destructors, exceptions, RTTI, thread-local data, `.sdata` (the build uses
`-msdata=none`, as `Makefile.runtime` does), and calls to any libogc or
RiftWii symbol by name. Everything the plugin needs from outside comes
through the function table it is given (section 4). The build's blob check
refuses a plugin whose link map holds an undefined symbol or a section
outside this list. Constructors, destructors and exception tables are kept
by `plugins/plugin.ld` and refused by name, never silently dropped: a link
that discarded them would load a plugin with a global never constructed, or
one that ends RiftWii at its first throw.

### Header

All fields are big-endian, the header is 128 bytes, and everything past it
is the image:

| Field | Meaning |
| --- | --- |
| magic | `RWPL` and a one-byte header version |
| name | Up to 16 characters, `A-Za-z0-9_-`, the same as the file's base name |
| plugin version | A number the plugin's own table reports, for logs |
| API version | The version of the function table the plugin was built against (section 4) |
| build stamp | The RiftWii build the plugin was made with: the same version string and commit as the DOL prints (`YYMM-N` plus the short hash of the checkout, or `dev` plus the hash of the source files for a local build) |
| entry table | Offsets of `init`, `shutdown` and the plugin's own table inside the image (all 24 bytes of the table, `kPluginTableBytes`, not just its first words) |
| sizes | Image size, `.bss` size, relocation count and the state size the plugin wants |
| flags | Whether the plugin can be unloaded; which memory it needs (section 3) |
| hash | SHA-256 of the header (this field zeroed), the image and the relocations after it, so a half-written or damaged file is caught before it runs |

The byte offsets are in `include/riftwii/pluginfile.hpp`; the relocation
list follows the image, 12 bytes per record (offset, type, target).

The image's first word is reserved, a zero that `plugins/plugin.ld`
places before any code: a shutdown offset of 0 means "none", so a
function linked at offset 0 could not be named. The first `gamecube`
plugin had its `rwp_shutdown` there, and the loader refused it at every
load (its table said the arena's base, the header said none). Now
`make_plugin.py` refuses any entry or table pointer at offset 0 when it
builds a plugin, and the loader refuses an init, probe, shutdown or launch
there.

### Refusal

The loader checks, in this order, and stops at the first failure with one
plain sentence in the log and on screen:

1. Magic, header version, sizes inside the file and inside the budget:
   "The GameCube add-on file is damaged. Update RiftWii again."
2. Hash: the same message.
3. API version not equal to the loader's: "The GameCube add-on is from a
   different RiftWii (API 3, this one is 4). Update RiftWii again."
4. Build stamp not equal to the DOL's: the same message with both stamps.
5. After the relocation, the plugin's own table: its size and API, its
   `probe` NULL or a word inside the image, its `shutdown` the header's
   (`plugin_check_table`): the damaged message, before `init` runs.

An older plugin next to a newer DOL (a card from before an update, or a
copy from an earlier zip) is therefore never run. The feature it provides
stays off, its menu entries show as unavailable with the sentence above,
and the rest of RiftWii works. Nothing is ever loaded in a degraded mode.
Version and build stamp are both checked because the stamp catches a
rebuild with the same API number and the API number gives a clearer
message when only the table changed; a local build stamps both the DOL and
its plugins in one `make`, so developers meet the check only when they copy
files by hand.

### Where the files live

Two places were weighed.

- `sd:/riftwii/plugins/` sits with the player's data: themes, covers, saves,
  settings. Players copy, back up and delete that folder, and a file there
  looks like theirs to change.
- `sd:/apps/riftwii/plugins/` sits next to `boot.dol`, in the folder that
  the zip already unpacks and the Homebrew Channel already treats as the
  application.

**Proposed: `sd:/apps/riftwii/plugins/`.** A plugin is program code that
must match one specific `boot.dol` exactly; it belongs to the application
folder, not to the player's data, and anyone replacing `boot.dol` by hand
(or unpacking a zip over it) then replaces the plugins too. The in-app
update already writes into `sd:/apps`: `AppsFileAllowed` in
`wii/online/online.cpp` lists what it may touch, and the plugin folder
becomes one more entry there, written by the same apps part of
`riftwii-update.pack`. The cost is that a loader started from another
folder, for example by `wiiload` from a development tree, must find its
plugins next to the DOL that is running: the loader takes the directory of
its own `argv[0]` (what the Homebrew Channel passes) and falls back to
`sd:/apps/riftwii/`. A development override, `plugins = <path>` in
`sd:/riftwii/settings.txt`, is allowed for tests.

### Naming across updates

The in-app update writes several files; a power cut between them must not
leave a card the old DOL refuses. Two options:

- **Fixed names** (`gcnative.rwp`). Simple. Update order matters: write the
  plugins, then the DOL last. A cut in between leaves the old DOL with new
  plugins, which the old DOL refuses until the update is run again.
- **Stamped names** (`gcnative.2610-440.rwp`). The old DOL still finds its
  own plugins until the new DOL is written, after which the new DOL
  deletes the other stamps. More files briefly; no refusal window.

Stamped names are proposed for the cost of a few lines in the loader and
the pack. This is a question for the maintainer (section 9).

## 3. Loading

**Decided: plugins load automatically on first use.** A plugin is read
into MEM2 the first time RiftWii needs it, for example when a player
presses Play on a GameCube game or opens a Settings row the plugin owns. It
costs nothing until then, and it stays loaded until RiftWii closes. A source
the player has hidden with the existing settings (such as Games from) never
asks, so its plugin is never loaded. Nothing is loaded at start-up except
what the menu needs to draw its first frame.

There are **no per-plugin on/off switches** for the built-in plugins. The
reason: everything ships together in one bundle, so a plugin is never
missing by choice, and memory is only spent on what is used, so a switch
would add a setting without saving anything. Hiding a feature stays the job
of the settings that exist.

1. Open `<plugins dir>/<name>.<stamp>.rwp` through RiftWii's own file
   layer (the same `sd:` mount the rest of the loader uses).
2. Read the 128-byte header and check it (section 2) *before* allocating.
3. Allocate the image, `.bss` and state in MEM2 (below), read the image in,
   apply relocations.
4. `DCFlushRange` the image and `ICInvalidateRange` it, then `ICSync`/a
   `sync; isync` pair before the first call. The virtual SD card install
   (`wii/hooks/vsdhook.cpp`) does exactly this after writing code into
   MEM2, and the resident runtime for code builds runs from MEM2 under the
   same rules; the plugin loader reuses that sequence rather than inventing
   one.
5. Call `init(api)`. A non-zero return unloads it again and logs the
   plugin's own reason.

Where the plugin goes in MEM2: the loader's MEM2 heap runs from
above the start-up stash's copy (`0x90809000` up, about 927 KiB, after the
restart snapshot; MEMORY.md) to just under IOS at
`0x933E0000`. The current users inside it are the menu's covers and
decoded art, the update pack and file downloads, the screenshot buffer
and the game being read for a launch. A game launch then puts its own
data at the arena ends, and a GameCube launch uses `0x90000000`–`0x91500000`
([Memory](ARCHITECTURE.md#memory)). A plugin therefore:

- is allocated from the **top** of the loader's MEM2 heap downward (a small
  arena of its own, sized from the largest plugin plus its `.bss` and
  state, a few hundred KB at most), so it does not fragment the heap used
  for covers and downloads, and does not sit inside any range a launch
  writes. The arena's size is set by the budget table in the first stage's
  measurement, not guessed here;
- must not be needed after a launch hands over. The GameCube loader's last
  act is a jump into the game; nothing of it is called after that, and
  the blob it left under the FST is a separate thing the loader places
  (and which does not live in the plugin's memory). The launch hand-off
  calls `shutdown` and then forgets the arena.

**Unload.** Plugins are kept until RiftWii closes. Dropping one under
memory pressure is optional: a plugin flagged unloadable, with no open
handles, may be dropped (`shutdown` first, then its arena released) when the
heap would otherwise fail an allocation. Stage (a) frees everything at
launch and does not need more; stage (b) measures whether any real plugin
does.

A plugin that fails to load never takes the menu down: the call site gets
a plain "not available" and a `logf` line saying which check failed.

## 4. The API

Two tables, both plain structures of function pointers, both versioned.

### The table RiftWii gives the plugin

Passed to `init`. It starts with its own size and the API version, so a
plugin built for a longer table than it received refuses to start instead
of calling a pointer past the end. It stays small; each group below is
a handful of functions, not a wrapper around libogc.

| Group | What a plugin can ask for |
| --- | --- |
| Log | `logf` with the plugin's name prefixed in the line, so every log line names where it came from |
| Memory | Allocate and free from the plugin's own MEM2 arena; allocate and free from the loader's MEM1 heap (for short-lived buffers that must be in MEM1); cache flush and invalidate for ranges |
| Files | Open, read, write, seek, size, directory listing and stat on `sd:` and the USB mounts, through RiftWii's own layers (the FAT32/NTFS/WBFS engines under `src/fs/` and the mounts `wii/storage/` sets up), never libogc directly; a read of a sector range of an image for the launch code |
| Menu | Add a source to the Home screen (a title, an icon the plugin supplies as a small ready texture, a callback to list games), add an action to a game's page, add rows to Settings; every string given as English and passed through the loader's `tr()` (section 5) |
| Launch | Hand a prepared launch (a plan from `LaunchModel`, a chosen IOS, the SD/USB image) to the loader's boot code; ask for the controller state; ask for the video mode |
| System | The few things a launcher needs: shut down the loader's IPC cleanly, reload an IOS, read the time |
| Crash | Register a name and base address for the crash screen (section 8) |

The table never exposes the loader's `riftwii::wii` classes. It exposes
what the function would do, so a menu refactor does not break plugins.
Pieces of `src/` that both the loader and a plugin need (the vcdiff reader,
the GameCube FST rewrite) are linked into the plugin that needs them, or
into the DOL and handed over by a function, never both; which one is
decided per function by size.

### The table the plugin gives RiftWii

Returned from `init` (as the entry table's third offset):

- **probe**: given a mounted drive, report whether the plugin has games
  there, cheaply (a directory test, not a scan).
- **list**: add the games it finds to RiftWii's game list, in the shape the
  menu already uses for Wii games (id, title, a cover key, a flag for which
  source it is). This is what `wii/launch/gclaunch.cpp` does today for
  `sd:/games/<folder>/game.iso`.
- **details**: for one game, the facts the game page shows (region,
  size, features the plugin can offer, its extra rows).
- **launch**: start it. Does not return on success.
- **shutdown**: release what `init` took.

A plugin implements only what its kind needs; absent entries are null.
There is no plugin-to-plugin call.

### Staying stable

"Stable" means two things here, and only one is promised.

- **Within a build, nothing moves.** A plugin and its DOL are built together
  and checked together (build stamp), so internal consistency does not
  depend on freezing anything.
- **Across builds, the API number tells the truth.** The table has a
  version; any change to the layout or meaning of a function bumps it. The
  version is written into every plugin by the build from a single header
  (`include/riftwii/plugin_api.h`, to be created in stage (a)), and the same
  header is what the loader builds against, so the two cannot drift
  silently. Adding a function at the end of a group is an additive change
  and does not need a bump if the plugin checks the table's size field
  before using it; anything else does.

Because everything ships together and a mismatch is refused, an early
mistake in the table costs a rebuild of RiftWii and its plugins, not a
broken ecosystem. Section 8 comes back to the risk of freezing too early.

## 5. Translations and strings

Every menu string is English in the source, goes through `tr()` and has
its six translations in `tools/lang_source.py` (AGENTS.md). Plugins keep
that rule.

- A plugin shows a string by giving the loader the English text through
  the menu part of the API; the loader's `tr()` looks it up in the
  translation table the DOL already carries (`wii/core/i18n.cpp`,
  `wii/lang/*.po`, a card's own `.po` on top) and returns the translated
  text, copied into the loader's memory.
- `tools/lang_source.py` already checks that every string in `T`, `KO` and
  `FR` still appears in the code. Its search is extended in stage (a) to
  look in `plugins/<name>/` as well, so a plugin's string without its six
  translations fails `lang_source.py` and CI like any other.
- New Japanese text must exist in the menu font
  (`tools/make_menu_font.py`) as before.

Where the translated text *lives* is the one open point. In stage (a) and
(b) the plugin's strings stay in the DOL's tables (a few KB per plugin,
all languages together): simplest, and the saving is the code. If the
strings of a large plugin prove a real part of what stays in MEM1, a
later stage lets a plugin carry its own table, `<name>.<lang>.po` packed
beside it and loaded with it, with `lang_source.py` writing one set of
files per plugin. That changes where `lang_source.py` writes, so it is
left for a later decision (section 9).

Log lines from a plugin follow the same rules as the loader's: plain
sentences, US spelling where players read them, and the plugin's name at
the start of each line.

## 6. First candidates

Sizes below are estimates from source sizes and the shape of the code, not
measurements; stage (a)'s first job is to produce a table from the linker
map of a real build (`size` on the ELF by object file) and replace these.

| Rank | Candidate | Estimated MEM1 saving | What it would take |
| --- | --- | --- | --- |
| 1 | **The GameCube native loader** | The largest single block: `plugins/gamecube/gcnative.cpp` is about 125 KB of source, with `gclaunch.cpp` and `gcpatch.cpp` another 33 KB, plus the vcdiff reader, the overlay and layout code and the GameCube memory card tools in `src/`, and the embedded `runtime/gc` blobs (compressed in the DOL). A rough first guess is 100-200 KB of image, to be measured | The cleanest cut: the code has one entry (a launch) and one list function. It needs the Launch group of the API; `PrepareGcGameInMenu` in `rift_menu.cpp` and the Home source for GC games move behind the plugin's list/details. The shared helpers (patch application, the FST rewrite used for Riivolution packs) must be split into what only GC uses (goes in the plugin) and what Wii launches also use (stays). The plugin needs its blobs from the DOL: they move into the plugin's file, which also moves their compressed bytes out of MEM1 |
| 2 | **Code-build tools** (Project+ and the like) | Tens of KB: the code-build planning in `gameextras.cpp`, the code list placement and hook code in `src/patching/`, the MEM2 veneer logic | Harder than rank 1 because code builds touch the Wii launch path (`boot.cpp`) at several points and the cheat code handler is shared with ordinary cheats. It would take a defined set of hook points (before apploader, after, before jump) in the API, so a plugin can add its work to a launch without being the launch. Do not attempt before the hook list is clear from stage (b) |
| 3 | **Emulator launchers** (Not64, mGBA: a user request) | None today (they do not exist), which is the point: each would otherwise add to the DOL. Small each (a launcher is a loader for another DOL plus a game list) | They need the generic parts to be in place: probe/list/launch for a non-disc source, a way to start another DOL with arguments (the headless code path in `wii/launch/headless.cpp` points the way), and files for ROMs in the file layer. Each is a plugin of a few KB plus whatever the emulator's own DOL is, and the emulator DOL is a separate question (it is not RiftWii's to ship unless its license allows; this document does not decide that). Worth listing because they are the case where the plugin shape is right and a DOL feature would be wrong |

Other things left out on purpose: the online (WFC) code and the RVZ reader
are used by ordinary Wii launches and stay in the DOL; the theme and cover
code are too tied to the first frame to defer.

## 7. Build and release

**Make targets.** `Makefile.plugin` (new in stage (a)) with one rule per
plugin under `plugins/<name>/`, driven by `Makefile.wii` like
`runtime-blob` is: `make -f Makefile.wii plugins` builds all of them;
`make -f Makefile.wii` still produces the DOL, and now depends on the
plugin step so that the DOL and its plugins carry the same stamp. The
rules reuse `Makefile.runtime`'s flags and its two-base link check;
`tools/rtreloc.py` writes the relocation list and a new
`tools/make_plugin.py` writes the header and the hash. The build fails if
a plugin has an undefined symbol, a forbidden section, an image larger
than the arena budget, or two links that differ beyond relocations.

**Source layout.** `plugins/<name>/` for a plugin's own sources, using
`src/` for shared platform-free code as everything else does. The
"no two sources share a base name" rule (AGENTS.md) covers plugin sources
too, since one flat output folder is how devkitPPC builds here.

**Release.** Today the zip is packed by hand (DEVELOPING.md, "Releasing"):
`sd-card/apps/riftwii/` with `boot.dol`, `meta.xml`, `icon.png`,
`music.ogg`. Plugins join as `sd-card/apps/riftwii/plugins/*.rwp`, in
the same step and in the same zip, so there are still exactly three
release assets. When the nightly draft's `tools/make_release_zip.py`
lands, that script packs them; until then the manual step in DEVELOPING.md
grows one line. `tools/make_update_pack.py` already carries the zip's
`sd-card/apps` files besides the DOL, `meta.xml` and `music.ogg`; the
plugin files ride in that same apps part, and `AppsFileAllowed` gains the
`riftwii/plugins/` prefix (and only `.rwp` files in it) so the pack's
reader still refuses anything else. The apps part is written before
`boot.dol`, or by stamped name as in section 2.

**The channel.** The Wii Menu channel is a forwarder that starts
`sd:/apps/riftwii/boot.dol` ([CHANNEL.md](CHANNEL.md)), so plugins in that
folder are found with no change to the channel. A DOL started by the
channel or the Homebrew Channel gets its directory from `argv[0]`
or the fallback, as above.

**Warm restarts.** A restart ([ARCHITECTURE.md](ARCHITECTURE.md#restarts-and-crashes))
re-enters `__app_start` with the writable data copied back from
`0x90800000` and starts exactly like a first run. A plugin's memory is
not part of that snapshot and is gone after a restart: the new run
loads again on demand. That is the safe behaviour (a plugin never
survives into a state it did not initialise) and costs one more file read.

**CI.** The three CI jobs (host tests; Wii and channel builds; translations)
gain these checks:

- the Wii job builds the plugins and runs the blob check;
- a size check: every plugin image plus `.bss` within its budget, and the
  DOL not larger than its limit, so a change that moves code back into
  the DOL is visible;
- a version match check: every `.rwp` in the build output carries the
  DOL's build stamp and the current API number;
- the translations job covers `plugins/`.

Host tests cover the parts with no hardware in them: the header parser
and the refusals, the relocation applier, and the function table's size
and version rules, in a new `tests/plugin_tests.cpp`.

## 8. Risks

**Code in MEM2.** The design relies on MEM2 being executable and cacheable
for code the loader writes. Evidence it works: the virtual SD card blob
and the resident runtime for code builds already run from MEM2 under
libogc's default BAT setup, and `vsdhook.cpp` flushes and invalidates
around the copy. What is not yet shown is *the loader itself* calling into
MEM2 code on a real Wii (not just Dolphin, which does not model BAT
permission faults the same way). Stage (a)'s test plugin exists to answer
this before anything real moves: Dolphin first, then a real Wii with
the d2x cIOS and with the original IOS 58 (the Wii-only checks AGENTS.md
says cannot be called tested in Dolphin). If it fails, the fallback is
setting a BAT for the arena in the plugin loader, which libogc allows; the
decision there is to treat it as a design failure and stop, not to hack
around it.

**Debugging and crash reports.** A crash inside a plugin shows a PC in MEM2
that `addr2line -e riftwii.elf` cannot resolve. Required from stage (a):

- the plugin's name, base address and build stamp are kept in a small
  table the loader owns;
- the crash screen (`wii/core/crash.cpp`) and `sd:/riftwii/crash.txt` print,
  for every address in the report that falls inside a plugin's arena,
  `plugin gcnative +0x1a40` instead of a bare address; the log has the
  load address at load time;
- the plugin build writes a map file and a stripped-but-symbolic ELF per
  plugin, and the release notes' "keep `riftwii.elf`" advice grows to "keep
  the plugins' ELF files" (the zip can carry them in a `symbols/` folder
  that the problem report names, not the player's card);
- the problem report names every loaded plugin with its stamp.

The in-game crash screen (`runtime/fault/`) and the GameCube blob's crash
screen are unaffected: they are in the game, not in a plugin.

**An API frozen too early.** The first design of a function table is the
one most likely to be wrong, and the first plugin to move (the GameCube
loader) is a large one. The mitigation is the build stamp rule: the API
has no outside users and every release ships a matching set, so a wrong
table is changed in the next build with no compatibility cost. The risk
is a team that treats the version number as a promise. The document
says plainly that it is not one. A second mitigation is the order of the
stages: the table starts as only what the test plugin needs, grows with the
GameCube loader's real needs, and only then is looked at again for
others.

Smaller risks:

- **Two ways to fail.** A card lacking a plugin (an interrupted copy) must
  read as "update again", not as "GameCube games vanished". The list shows
  the unavailable source with the plain sentence, and a plugin-needed
  setting is not silently dropped from the settings file.
- **MEM2 pressure.** The start-up stash keeps about 927 KiB of MEM2 for the
  whole run (MEMORY.md); both together must not starve the covers and
  downloads. The plugin arena's
  size is therefore in the memory map (ARCHITECTURE.md, Memory) once
  built, and the heap check in `session.log` prints it.
- **The unseen cost of splitting.** Code shared between the DOL and a
  plugin is either duplicated (size) or called through the table (a
  dependency). Moving the GameCube loader will show which; the size check
  in CI keeps a drift visible.
- **Performance.** Every call into a plugin is an indirect branch through
  a table. Nothing that runs per frame is proposed for a plugin.
- **Clean room.** The design is RiftWii's own and uses names from this
  project (`.rwp`, function tables, build stamp). It follows no other
  loader's plugin interface, and a plugin must be written from this
  document and the code here, not ported from elsewhere (AGENTS.md).

## 9. Staged plan

Each stage is its own pull request, and each ends with the host tests,
the Wii and channel builds, and the translation check passing, plus
Dolphin for anything a player can see.

1. **(a) Loader and API skeleton, with a test plugin.**
   `include/riftwii/plugin_api.h`, the header and refusals
   (`src/` portable code, host-tested), the loader in `wii/core/`, the
   MEM2 arena, the debugging table in the crash screen,
   `Makefile.plugin`, `tools/make_plugin.py`, the CI checks, and one
   plugin, `selftest`, that logs, reads a file, adds one Settings row
   with a `tr()` string and a Home source with one fake game. Update
   pack and zip carry it. Measure the real sizes and replace the
   estimates in this document. Test in Dolphin, then on a real Wii
   (IOS 58 and d2x) for the MEM2 code question. No existing feature
   moves; `selftest` is removed or kept behind a debug flag at the end of
   the stage. *Built* ("Stage (a) as built" above), except the Settings
   row and the Home source: the Menu group of the API came out of stage
   (a) and goes with the GameCube loader's real needs in (b). `selftest`
   ships, loaded only by the test switch.
2. **(b) Move the GameCube loader.** `plugins/gcnative/`, the Launch group
   of the API grown to what it needs, `gclaunch.cpp`, `gcnative.cpp` and
   `gcpatch.cpp` and the GC-only helpers out of the DOL. Report the
   measured MEM1 saving, and that every GameCube checklist item
   (HOME, saves, 480p, 16:9, crash screen, RVZ) still passes. Changes to
   behavior are out of scope: this is a move.
3. **(c) Others, one at a time:** code-build tools if the hook points
   hold up, then each emulator launcher a player asks for.

Stages (b) and (c) wait on the maintainer's answers below and on the MEM2
start-up work, whose memory map they must not contradict.

### Decisions for the maintainer

- Is the format right: a relocatable image with the header in section 2,
  or should plugins be restricted to pure PIC C like the in-game blobs?
- Location: `sd:/apps/riftwii/plugins/` (proposed) or `sd:/riftwii/plugins/`.
- Fixed file names with the DOL written last, or names stamped with the
  build.
- Plugin strings stay in the DOL's tables (proposed) or each plugin carries
  its own translation files later.
- Is the GameCube loader the right first move, and is stage (a) allowed to
  add a `selftest` plugin to the release for one version?
- Is "the API is private and changes freely" acceptable as the rule?
