# From 2.7.0 to Stable 2610

Everything that changed between the first stable, RiftWii 2.7.0 (October 3, 2026), and the second, **RiftWii Stable 2610-325** (October 11, 2026). In between came the 2.8.0 to 3.3.9 betas, the 2610 betas and 2610 RC1 to RC3.

## Version numbers

RiftWii now numbers its builds like Dolphin: **2610-325** is the 2610 release line (year 26, month 10) plus the number of changes since 2.7.0. A bigger number is always newer. Settings > Updates picks Automatic, Stable or Beta; a stable build on Automatic follows Stable.

## The menu

- **The Wii look:** the default look now follows the Wii Menu: a bar that dips, an LCD clock (12 or 24 hour), the date under it, round buttons and a Wii-style HOME Menu (Homebrew Channel, Wii Menu, Priiloader, power off, Wii Remote batteries).
- **The Wii Menu's own font**, read from your Wii's NAND (Settings > Menu font) and cached on the SD card.
- **Three ways to see your games** (Settings > Home tiles):
  - **Covers**, with box art from GameTDB.
  - **Channels**: your games' real banners, animated and with their sounds, as on the Wii Menu. Arrows go from banner to banner, and Start on a banner goes straight to the game.
  - **The shelf**: your games stand as boxes, spine out. The one you're on slides out with a reflection on the shelf and a blurred backdrop from its cover. Up and Down turn the box round to its back; hold Left or Right and it speeds up; with a Wii Remote, hold A and drag it.
- **Themes:** Bookshelf (wood) and Midnight (dark) come in the zip, and anyone can make one with the theme kit (pictures, colours, music). The Wii look stays the default, and Settings > Theme can always go back to it.
- **Transitions:** pages zoom, slide and pop like the Wii Menu; a shelf box flies to the game page.
- **Search** games by name or game ID (on-screen keyboard; Z on a GameCube controller).
- **Favourites** (button 1 on a banner), **Home order** (A to Z, recently played, most played), **Games from** (all, SD, USB, disc), **play history**, and a letter jump.
- **16:9:** a widescreen menu with Settings > Screen size, and banners shown at their full width.
- **What's new** opens once after an update with the version's main changes (Settings > What's new shows it again).
- **The Disc Channel** can be on Home (Settings > Disc Channel), and the disc drive has a box of its own.
- **Languages:** French and Korean join English, Spanish, Japanese, Portuguese and Italian, and every menu is translated in all of them. Korean needs the Wii Menu font of a Korean Wii.
- **You can now disable the clock.**
- Smoother Wii Remote pointer (steadied tilt and a 1 euro filter), and the GameCube and Classic Controller pointers start where the Wii Remote points.

## Games and mods

- **Saves for SD and USB games** go through d2x's NAND emulation, as USB Loader GX does, and the runtime's FAT code is safer: an open file is never deleted or replaced, a folder with files in it is not deleted, every long name gets its own short name, and big folders stop at FAT's own limit.
- **More games work:** Resident Evil 4 from RVZ, Mario Super Sluggers (keeps its picture width), Need for Speed: ProStreet with screenshots, Wii Fit Plus and others with a Balance Board connected.
- **Per-game settings** from USB Loader GX: aspect ratio, rumble, Wii Remote speaker, region strings fix, and the game's cIOS.
- **Cheats:** codes with values (X) get a keypad, and several code lists are merged.
- **Pack titles** and live search names; mod pictures on the Mods page; file patches stacked on one file read once.
- **Two USB drives** and USB port 2, an **SD image maker** on the Wii (for code builds' virtual SD card), and split and several .raw images.
- **Launches from other loaders:** USB Loader GX (`--launch`) and a WiiFlow plugin can start a game through RiftWii with your mods.
- **Wiinnertag / RiiTag** updates when a game starts.
- **Cars 2 is cursed:** RiftWii warns before you play it (and Toy Story 3 and Disney Infinity, which share its engine). Turn off the GameCube adapter, screenshots, cheats and mods for it.

## Controllers

- **GameCube controller adapter** (official and third-party) in the menu and in games, on a Wii and on a Wii U, with **Check each cIOS** to see which cIOS reaches it. On a Wii U, SD games start on a d2x cIOS on base 58, the one that reaches the front USB ports. The adapter's pads can answer every popup, the startup ones too.
- **Wii Remote rumble** in the menu.

## The RiftWii channel

- **Channel 11** empties the CPU's L2 cache before RiftWii starts, which fixes a black screen when a game's HOME Menu > Wii Menu goes back to RiftWii. RiftWii asks you to update an older channel before Home opens (Update now runs the installer and comes back).
- Earlier: channel 9 (hardware access), channel 10 (full width on 16:9 with its splash).

## Reliability

- **Problem reports** include the channel log, the d2x and cIOS details and the adapter check; a game that crashed offers a report on the next start, with a crash screen.
- **d2x check:** d2x v11 beta 3 or newer is needed for USB and SD games; v11 beta 1 and 2 get a reminder with a QR code, older and non-d2x cIOSes are refused in the menu.
- Hardening from a full code review: safer settings writes, real network timeouts (TLS included), banner and font checks, RVZ and WBFS bounds, NAND permissions closed after use.
- The in-app update keeps the themes and the channel installer up to date (`riftwii-update.pack`).

## Known issues

- Back to RiftWii can still go black after some games' HOME Menu > Wii Menu ([#62](https://github.com/KakarottoCake/Riftwii/issues/62)); set Settings > Wii Menu button to Wii Menu if it does.
- The GameCube adapter stays off in code builds (Project+, PMEX Remix, REX).
- Kirby's pink bar with width 720 and borders removed (#13), and Black borders: Remove all in some games (#15).
