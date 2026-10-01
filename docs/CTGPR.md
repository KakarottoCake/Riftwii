# CTGP-R 1.03 from RiftWii

Status for 2.7.0 RC2: **experimental, untested on a Wii.** The Guide keeps
telling players to start CTGP from the Homebrew Channel until a console run
reaches an offline race.

## How RiftWii starts it

CTGP-R 1.03's Riivolution XML replaces `main.dol` with the CTGP-R Channel
(`/apps/ctgpr/boot.dol`). `homebrew_app_stand_in` (`wii/modplan.cpp`)
recognises a choice that only does that, and `RunLaunch` (`wii/autorun.cpp`)
starts the app the way the Homebrew Channel does (`StartHomebrewApp`), not
through the apploader. The save redirect in CTGP's XML cannot apply and is
dropped.

* **Disc:** the app starts under the menu's IOS with hardware access, before
  any probe or IOS reload.
* **ISO/WBFS on USB or SD:** d2x is set up to serve the image as the disc,
  then the app runs under the cIOS. CTGP reloads IOS itself (its channel asks
  for IOS37), which on its own would drop d2x and the virtual disc. Since RC2
  the activation arms d2x's IOS reload block first (below), so those reloads
  land in the same cIOS.
* **RVZ:** refused. RiftWii's PPC resident code reads an RVZ; d2x only holds
  its stub, and nothing of RiftWii survives into CTGP's later stages.

## Hardware access under the cIOS

CTGP's `meta.xml` asks the Homebrew Channel for hardware access
(`<ahb_access/>`), and an IOS reload ends it. On 2.7.0 RC6 a tester's
Wii (USB, NTFS, RMCP01, IOS249) did everything up to the handoff:
d2x accepted the reload block and served the disc, and the app started
under IOS249 with "hardware access off". The CTGP channel then showed a
green screen.

Since 2.7.0 RC7, `keep_hardware_access` (`wii/boot.cpp`) changes the
running IOS's ES right before the reload into the cIOS so the cIOS keeps
hardware access, as USB Loader GX and WiiFlow do with libruntimeiospatch's
`IosPatch_AHBPROT`. It does the same to the cIOS's ES once d2x is set up,
for CTGP's own reloads. Only the Homebrew Channel app path does this, not
ordinary game launches. `boot.log` shows `IOS<n>'s ES keeps hardware
access on for the next IOS` for each, and `Homebrew app: IOS<n>, hardware
access on` when it worked. Untested on a Wii.

## d2x IOS reload block

ES ioctlv `0xA0` on `/dev/es`, two 4-byte inputs: mode 2 and the cIOS slot
to load in place of any IOS the program asks for (as USB Loader GX's
`BlockIOSReload` sends it). Mode 0 with one input clears it.
`di::set_ios_reload_block` (`wii/di.cpp`) sends it; `tests/d2xreload_tests.cpp`
runs the real `wii/di.cpp` against a mocked IOS (`tests/wii_mock/`) to check
the vectors, alignment, slot bounds (200-255) and error handling.

It is armed in `activate_image_game` right after the F6 reset-disable,
**before the first partition probe**: d2x hides its ES commands once ES has
identified the disc's title. A new fragment configuration (a fresh cIOS
reload) forgets it.

If the probe or the app's start fails, RiftWii tries to clear it; otherwise
every later IOS reload, including the menu's own, would land in the cIOS.
Whether d2x accepts the clear after a probe has identified the title is
unknown. When it refuses, the error tells the player to restart the console.
`boot.log` shows `d2x keeps IOS<n> across the program's own IOS reloads`
when armed and `IOS reload block cleared` / `could not be cleared` after a
failure.

## What a console test must show

1. A USB or SD ISO/WBFS of Mario Kart Wii with CTGP's pack enabled reaches
   the CTGP channel, then an offline race.
2. Save and load work, and relaunching works.
3. A failed start (rename `/apps/ctgpr/boot.dol`) logs the clear, and the
   menu can still start another game.
4. A real disc, as the control.

Dolphin cannot test this: its HLE IOS has no d2x.

## Research notes (CTGP-R 1.03.1190 All Inclusive)

Why the resident runtime route (the pack's `main.dol` loaded as the game)
does not work:

1. `install_resident` needs `IOS_IoctlAsync`; the host `ipcscan` finds no
   IPC-shaped function in the packed launcher, the unpacked boot runtime or
   the unpacked channel.
2. The channel uses synchronous IPC for its disc commands (disc ID `0x70`,
   partition open `0x8B`, reads `0x71`, unencrypted reads `0x8D`); the
   resident redirects only the asynchronous path.
3. The first executable is not the last: CTGP unpacks a boot runtime at
   `0x80780000` and the channel at `0x80800000`. Hooks in the stub do not
   carry over.
4. CTGP has its own IOS reload paths (boot runtime and channel; the channel
   asks for IOS37 on its low-IOS path). `preserve_current_ios` only covers
   RiftWii's own launch preparation.

If d2x cannot satisfy CTGP's IOS setup, the next step is an IOS-side virtual
disc that survives ES reloads, starting from the MIT-licensed
[CTGP-R MSC launcher](https://github.com/mkwcat/ctgpr-msc) (`ios/IOS/EmuES.cpp`
shows the reload lifecycle; its `/dev/di` proxy is disabled and there is no
implemented disc resource manager).

These notes came from Ghidra pseudocode and the original binaries under
`D:/AI Projects/CTGPR DECOMP/`; no CTGP code or assets are in RiftWii.
Addresses describe that build only.

| Input | SHA-256 |
|---|---|
| `apps/ctgpr/boot.dol` | `b4c6ba27bc22813ecad686e918e5dafe9cd2ce8afa84427aa60dd865d2fe4556` |
| extracted `boot_internal.dol` | `17a329273369e1586677a0016ab354f41e49d971d0f596d9f65c7d3f6f2ab6d6` |
| extracted `chan_main_internal.dol` | `e66fdf5cf14cbc48bc50132c9ca9de42d46993740557b89eead9e010ce5e1cc2` |
