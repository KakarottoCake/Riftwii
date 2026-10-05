# Attribution audit

RiftWii is GPL-3.0-or-later, like USB Loader GX and libwiigui, whose code
it follows in places. What the GPL asks of that is attribution: code taken
from another project keeps that project's notices and names its holders.
[NOTICE.md](../NOTICE.md) lists every piece RiftWii contains or follows.
This audit checks that list against the code itself instead of memory: a
file-by-file comparison of RiftWii's own source with USB Loader GX's and
with libwiigui's.

RiftWii was written with the help of AI tools, and USB Loader GX's source
was a reference for the features NOTICE.md credits to it. That is the
reason for checking mechanically rather than trusting what anyone
remembers reading.

## How to run it

```
git clone https://github.com/wiidev/usbloadergx
git -C usbloadergx checkout e25c4f3501ed957b7db73f79c51fdf00715ab2e2
py -3 tools/attribution_audit.py usbloadergx --out gx.md
py -3 tools/attribution_audit.py vendor-libgui --out libwiigui.md
```

`tools/attribution_audit.py` compares RiftWii's own code (everything under
`src/`, `include/`, `wii/`, `runtime/` and `channel/`; the third-party code
in `vendor-*` keeps its own headers and is left out) with the other tree
three ways:

1. **Copied code.** Each file is cut into C tokens, without comments,
   whitespace or `#include` lines, and fingerprinted the way MOSS does it
   (winnowing over runs of tokens). A run found in both projects is a
   match. This is done twice: with the tokens as written (25 in a row),
   and with every name replaced by the same placeholder (50 in a row),
   which finds code that was copied and then renamed.
2. **Function names** defined in both projects.
3. **Constants**: hex numbers of five digits or more found in both.

Every match was then looked at by hand. A short run of boilerplate, a
library's API or a hardware fact is shared by all Wii homebrew and is not
anyone's code.

## Results (October 2026)

Against USB Loader GX at commit `e25c4f3501ed957b7db73f79c51fdf00715ab2e2`
(the commit NOTICE.md cites), and against libwiigui 1.07 as vendored in
`vendor-libgui`.

### Copied or closely followed: attributed

| RiftWii file | Matches | Where from | Status |
| --- | --- | --- | --- |
| `wii/wfc.cpp`, `src/wfcpatch.cpp` | Wiimmfi's binary patch and the server patches | GX `source/patches/gamepatches.c` (Leseratte's Wiimmfi patches) | In NOTICE.md since they were added; holders in the files |
| `src/gxpatches.cpp`, `src/gxkirby.inc`, `src/codehook.cpp`, `src/gamelang.cpp` | Patch words and search patterns copied as data | GX `gamepatches.c`, `disc.c`, `kirbypatch.c`, `patchcode.c` | In NOTICE.md since they were added; holders in the files |
| `src/sysfont.cpp`, `wii/wiifont.*` | The Wii Menu font's two content hashes | GX `source/SystemMenu/SystemMenuResources.cpp` (Dimok, giantpune, blackb0x) | Added in this audit: NOTICE.md row, holders in the files, credits |
| `src/riitag.cpp`, `wii/riitagsend.*` | No copied text; the Wiinnertag file format and behaviour | GX `source/network/Wiinnertag.cpp` (Dimok, zlib) | Added in this audit: NOTICE.md row, holders in the files, credits |
| `wii/rift_menu.cpp` | 16 runs as written: the GUI thread and menu loop | libwiigui template `menu.cpp` (Tantric) | Added in this audit: Tantric named in the file and in NOTICE.md |
| `wii/main.cpp` | The start-up order and `ExitApp` | libwiigui template `demo.cpp` (Tantric) | Added in this audit, as above |
| `wii/skin.cpp` | `DrawRgb5a3`'s drawing, after `Menu_DrawImg` | libwiigui template `video.cpp` (Tantric) | Added in this audit, as above |
| `wii/boot.cpp` | A 14-byte pattern of IOS's ES code | libruntimeiospatch in GX's tree | In NOTICE.md: the bytes are IOS's, the code RiftWii's own |

### Not copied

- **Header boilerplate.** Most `runtime/*.h` files and a few others match
  libntfs, FreeType, zlib and FreeTypeGX headers only in shape: an include
  guard, `extern "C" {`, then a list of `#define`s or prototypes. With
  every name replaced, any two such headers look alike.
- **Byte tables.** `src/pngencode.cpp`, `wii/tlsroots.c` and
  `src/gxkirby.inc` match other byte tables (GX's code handler, wolfSSL's
  test certificates) only as long rows of hex numbers. `wii/tlsroots.c`
  holds public root certificates, the same bytes wherever they appear.
- **Look-alike code.** `wii/rift_menu.cpp` against GX's file browser
  (button set-up), `wii/crash.cpp` and `wii/rift_menu.cpp` against pugixml
  (a run of `using` lines shaped like a run of `case` lines), `src/fat32.cpp`
  (a block of field assignments) and `runtime/shot/shot_hook.c` (a table).
- **Function names.** Shared names are either generic (`Draw`, `read`,
  `parse`, `size`, `Update`) or libwiigui's (`HaltGui`, `ResumeGui`,
  `UpdateGUI`, `InitGUIThreads`, `MainMenu`, `Menu_DrawRectangle`), which
  RiftWii and USB Loader GX both get from Tantric's template: that is the
  shared ancestry, now named in the three files that grew from it.
  `GuiGameGrid` shares only its name with GX's class; no code matched.
- **Constants.** The rest are Wii facts: the low-memory map
  (`0x80000020`, `0x800000F8`, `0x80003140`...), the bus and CPU clocks,
  the boot magic `0x0D15EA5E`, the Homebrew Channel's reload word, the code
  handler's addresses and the MEM1/MEM2 bounds, as documented on WiiBrew
  and used by every loader.

## What this does and doesn't show

A clean result means no run of code long enough to fingerprint was
found in both projects beyond what is listed above. It can't rule out
code that was rewritten line by line so thoroughly that its structure
changed; that is closer to writing new code than to copying, but where
RiftWii followed someone's design, NOTICE.md says so in words. If you find
something this missed, please open an issue: it gets the same treatment,
a NOTICE.md entry and the holders named in the file.
