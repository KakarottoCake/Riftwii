# WiiWare and Virtual Console: making them work every time

A working page for the effort to make WiiWare and Virtual Console games
start, save and return as they do from the Wii Menu. It keeps the plan,
the test results, and who is doing what. It's updated as the work goes.

## The goal

Every WiiWare and Virtual Console game a player has, on the Wii itself,
in an emulated NAND on the SD card, or moved to the SD card by the Wii
Menu, starts from RiftWii, saves, and goes back to RiftWii or the Wii
Menu.

## WAD folders: drop and play

`sd:/wiiware/` and every mounted USB partition's `wiiware/` folder are
read on Home's background drive worker, including one level of subfolders.
WADs appear as channel games and use the existing covers, box art, banner
and icon paths. The small header, ticket and title metadata are checked
on each scan; large contents are not read. Banner names and the decrypted
banner (which contains its icon) are cached under
`sd:/riftwii/wiiware-cache/`, by source path, metadata/header hash and
file size, plus modification time on SD. USB's raw file view has no time,
so a change confined to encrypted content with identical metadata and
size cannot be detected until that cache/preparation stamp is removed.
Normal rebuilt WADs change their title metadata's content hashes.
The scan stops at 1,024 WADs or 8,192 folder entries per drive scan.

A WAD is a delivery archive with encrypted contents, not the NAND files
ES opens while a game runs. The public [WAD format](https://wiibrew.org/wiki/WAD_files),
[ticket](https://wiibrew.org/wiki/Ticket) and
[title metadata](https://wiibrew.org/wiki/Title_metadata) describe those
sections and their content hashes. Together with the ES-content findings
below and the existing full NAND emulation path, this is why RiftWii
unpacks on first play rather than pretending the WAD itself is a NAND.
No other loader's source was used for this feature.

The key source is checked against the banner's content hash: console OTP
first, then the read-only [ES_Decrypt call](https://wiibrew.org/wiki//dev/es)
with [IOSC common-key handle 4](https://wiibrew.org/wiki/IOS/Syscalls), then
the player's BootMii `sd:/keys.bin`. ES decrypts the ticket's title key;
RiftWii does not export the common key or import a ticket into real NAND.
An incorrect nonzero OTP result falls through just like an unavailable
one. Each attempt and its expanded AES key are cleared after use, and only
the successful source is logged. Dolphin implements the ES call, but its
unfinished permission checks do not prove what every real IOS/d2x version
allows. A Wii without hardware access may still need its own BootMii
backup if IOS denies the call. See NOTICE.md for the Dolphin reference.

First play verifies and stages every content, then replaces the content
folder, ticket, title ownership list and shared-content map with rollback
on a reported error. A new full emulated NAND first gets only its missing
`SYSCONF`, `setting.txt`, `uid.sys` and `cert.sys` from the player's own Wii.
These stay on the SD card, never in the repository. Existing system files
are preserved. Each new title gets an unused owner in `uid.sys`, following
the public [ownership-list format](https://wiibrew.org/wiki//sys/uid.sys).
An update keeps the previous content until the replacement is complete;
it never changes the title's `data` save folder. The source stamp is
published inside the content folder, in `.riftwii-wad`, with the title.
A later start skips extraction only when the stamp, installed metadata,
ticket and every content's expected size agree. An identical manually
installed title can be reused without acquiring an automatic stamp.
Auto-prepared copies are listed only by the WAD scan, so removing their
WAD hides the tile without removing contents or saves. Previously
installed copies are merged by title ID, and a playable real-NAND copy
keeps its existing save/launch location.

The destination is the configured SD channel NAND, normally
`sd:/nands/01`. If another loader's configured destination is unusable
(for example on USB), WAD discovery uses that SD default. Saves remain at
`<NAND>/title/00010001/<eight hex digits>/data/`. Deleting the source WAD
or `sd:/riftwii/wiiware-cache/` does not delete saves. A damaged WAD or
space/write failure leaves no newly published partial title; details go
to `session.log`. A power loss during the short publication step can leave
`.riftwii-old` backups; RiftWii refuses to overwrite those on a retry,
so they can be checked before recovery. This is not a power-loss-safe
filesystem transaction. No WAD or console key is shipped or kept in the
repository. The existing common-key reader does not support Korean-key
or vWii-key WADs. WADs must be smaller than 2 GiB, metadata sections at
most 64 KiB, up to 512 contents, and banners at most 4 MiB.

Host tests cover parsing, cache records, source changes, folder depth,
raw USB partitions, duplicate merging, first/second play, failed updates
and save retention. Dolphin validation is assigned to the team tester;
real Wii checks are still needed for d2x SD and USB timing.

## Open reports

| Report | What happens |
| --- | --- |
| Super Mario 64 (VC, N64) | Doesn't start (problem report from a player). |
| Cruis'n USA (VC, N64) | Reported not starting. Starts and plays on grantendo's vWii from the Wii's own memory since 2610-689 (#323); a report that still fails may be the emulated NAND case, as Super Mario 64. |
| WarioWare: D.I.Y. Showcase on the Wii itself | Stopped after the cIOS line in `boot.log`; every start after it was a black screen until the Wii was switched off. #342 added log lines for each step and the clean-up at the next start. |
| Four Wii Remotes in some channels | Jack's work. |
| The GameCube pad's way back in N64 VC games | Doesn't leave the game: it waits for the game's main thread to pause, and the N64 emulator's never does (a second thread reads the pads). SNES VC and WiiWare games tried pause normally. grantendo has a fix (leave from the pad thread's pause after half a second) to test on a Wii U. |

## Test games

14 games, picked so that each way a channel can start is covered:

| Game | Kind | Why it's in the set |
| --- | --- | --- |
| WarioWare: D.I.Y. Showcase | WiiWare | The black-screen report |
| Super Mario 64 | VC, N64 | Reported not starting |
| Cruis'n USA | VC, N64 | Reported not starting |
| Dr. Mario Online Rx | WiiWare | Its program isn't a plain DOL |
| Fluidity | WiiWare | Same, from another developer |
| Super Mario Bros. | VC, NES | The NES emulator |
| Mario Adventure | VC, NES (fan-made) | A homemade VC package |
| Sonic the Hedgehog | VC, Genesis | Sega's emulator |
| Victory Run | VC, TurboGrafx-16 | Hudson's emulator |
| Final Fantasy Crystal Chronicles: My Life as a King | WiiWare | Downloadable extras |
| Mega Man 9 | WiiWare | Downloadable extras |
| Pokémon Rumble | WiiWare | Saves, several Wii Remotes |
| World of Goo | WiiWare | Big and popular |
| Tetris Party | WiiWare | Balance Board |

The WAD files come from the maintainer's own backups. They are never
put in the repository.

## Results

"Direct" is the game started by Dolphin itself, without RiftWii: it
shows whether Dolphin runs the game at all. "RiftWii" is the game started
by RiftWii's test build (`channelboot <title>` in `autorun.txt`) in the
Dolphin built with `tools/dolphin/es-identify.patch`, with no
`debug_off`. Both are a screenshot after about 50 and 75 seconds with no
input, the games installed on the Wii's own NAND.

| Game | Title | IOS | Direct | RiftWii in Dolphin | Real Wii |
| --- | --- | --- | --- | --- | --- |
| WarioWare: D.I.Y. Showcase | 00010001-WA4E | IOS55 | Creates its save | Creates its save | Not yet |
| Super Mario 64 | 00010001-NAAE | IOS9 | Title demo | Title demo | Not yet |
| Cruis'n USA | 00010001-NASE | IOS33 | Attract screens | Attract screens | Not yet |
| Dr. Mario Online Rx | 00010001-WDME | IOS37 | ESRB notice, title | Title screen | Not yet |
| Fluidity | 00010001-WFLE | IOS56 | Hold-the-remote screen | Hold-the-remote screen | Not yet |
| Super Mario Bros. | 00010001-FAGE | IOS9 | Title demo | Title demo | Not yet |
| Mario Adventure | 00010001-F1TE | IOS9 | Title screen | Title screen | Not yet |
| Sonic the Hedgehog | 00010001-MAHE | IOS9 | Demo play | Demo play | Not yet |
| Victory Run | 00010001-PAFE | IOS53 | Controller setup | Controller setup | Not yet |
| Final Fantasy Crystal Chronicles: My Life as a King | 00010001-WFCE | IOS37 | Creates its save | Creates its save | Not yet |
| Mega Man 9 | 00010001-WR9E | IOS37 | Player name | Player name | Not yet |
| Pokémon Rumble | 00010001-WPSE | IOS56 | Controller notice | Controller notice | Not yet |
| World of Goo | 00010001-WGOE | IOS35 | Title screen | Title screen | Not yet |
| Tetris Party | 00010001-WTPE | IOS35 | Title screen | Title screen | Not yet |

All 14 start through RiftWii, and 13 show the same screens as when
Dolphin starts them itself (2026-10-26, branch at 209b308c). Dr. Mario
Online Rx was already on its title screen at both RiftWii screenshots,
while the direct start still showed its ESRB notice first; that may only
be timing, not checked further. In WarioWare, Dolphin's log
shows the game asking ES for its title and data folder and reading its
own content through ES, as it does when started directly.

In the d2x Dolphin (2026-10-27, `d2x-dolphin` with this branch), World of
Goo also starts from an emulated NAND on the SD card (`channelboot
0001000157474F45 sd:/nands/ww`): the shared contents the copied NAND
lacked are copied in, the game reaches its title screen and writes its
save in the emulated NAND, and HOME > Wii Menu > Yes starts the RiftWii
channel (`d2x: launching 0001000155465457 in place of the Wii Menu`),
which starts RiftWii again. The other 13 games from an emulated NAND, and
all of it on a real Wii, are still to do.

A channel the Wii Menu moved to the SD card, Super Mario Bros. (VC, NES):
the real Wii Menu in this Dolphin moved it (Data Management > Channels >
Move), leaving only its TMD on the Wii and a 2.5 MB content.bin on the
card. RiftWii decrypted that with the default Dolphin console's keys.bin,
copied its four contents back (listed in staged_channels.txt), ran as the
game, and the game played its demo, reading its ROM through ES. The next
RiftWii start removed the four copies again, leaving the TMD as before.

Super Mario 64 (VC) started through RiftWii opens its HOME Menu over the
game: the emulator's second read through ES (content 4) works too. The
retry ticket's fakesigning uses the same bytes and rule as the channel
installer, whose own fakesigned files d2x Wiis and vWiis accept. That is
evidence for the signing only: the retry itself (ES_Identify with that
ticket on d2x) hasn't been tried on a Wii yet.

Saving and leaving, Final Fantasy Crystal Chronicles: My Life as a King
started through RiftWii: it made its save (ARCH.SAV, DAT0-2.SAV and the
banner.bin the Wii Menu shows, in /title/00010001/57464345/data) and
reached its main menu; started again, it went straight to "Press A Button
to Start" with no save prompt; HOME > Wii Menu > Yes landed on the Wii
Menu.

## Findings

### Dolphin answers `ES_Identify` with -1017, so no channel gets past its first save

RiftWii starts a channel the way a loader does: it asks IOS to run
RiftWii as that title (`ES_Identify`) and jumps to the game's program.
On a Wii, `ES_Identify` and `ES_DIVerify` are the same request (0x1C).
Dolphin's ES handles 0x1C from the game's side with one line that answers
-1017 (`ESDevice::DIVerify(const IOCtlVRequest&)`); the full version it
uses for discs (`DIVerify(tmd, ticket)`: title context, user and group
ID, data folder) is only reachable from its own disc code.

Every game asks ES which title it is (`ES_GetTitleID`) and where its data
folder is (`ES_GetDataDir`) as soon as its NAND library starts. Without a
title those fail, the game has no save folder, and its first save access
shows "An error occurred while accessing Wii system memory". That is what
WarioWare does here with `debug_off = identify`.

`tools/dolphin/es-identify.patch` adds the missing part to Felk's Dolphin
(the build in `build-dolphin/felk`): it reads the ticket and TMD from the
request and runs the disc steps, marking the title as a channel. A ready
Windows build is linked in [HARNESS.md](HARNESS.md), and the d2x Dolphin
(RiftWii/dolphin-felk-riftwii, `d2x-emulation`) has it built in.

### The Virtual Console reads its game through ES, not from files

From the N64 Virtual Console decompilation (zeldaret/oot-vc, which covers
Super Mario 64, Mario Kart 64 and Ocarina of Time; read as a reference,
nothing copied): the emulator opens its ROM archive as content 5 of the
running title and its HOME Menu files as content 4, through ES's "open
content of the running title" request. It never names a file. So a VC
game only starts when:

1. ES runs it as its own title (`ES_Identify` accepted), and
2. ES finds that title's content files where it looks: the real NAND, or
   d2x's emulated NAND when the game comes from one.

If either fails, the emulator stops at its first read, before it draws
anything: a black screen, as reported.

### Super Mario 64 from an emulated NAND on a vWii (report on 2610-693)

The player's report shows d2x's NAND emulation coming on for
`sd:/nands/01` on IOS247, 249, 250, 251 and 246, and ES refusing to run
RiftWii as NAAE01 with -1029 (invalid ticket) on every one: the emulated
NAND's own ticket. 2610-697 added a retry with a generic ticket for
exactly this case, so this report needs a new try on 2610-697 or later.

Two things the same report shows that are still open:

- On IOS248 and IOS252, d2x's FAT module couldn't mount that SD card
  (-101) and didn't let go of it. Each such try still costs an IOS
  reload. Remembering which IOS failed to mount the card, and skipping it
  for the rest of the session, would save time and leave the card alone.
- All eight IOSes were tried one after another: about 18 seconds before
  the failure was shown. When every d2x IOS gives the same ticket answer,
  trying more of them doesn't help.

### Still open (from a review of the channel start)

- **A missing content file still lets the game start.** Since 994137df
  RiftWii opens every needed content through ES after `ES_Identify` and
  names any it can't open in boot.log (checked in Dolphin: Super Mario 64
  with `/shared1` moved aside logs contents 2, 3 and 4), but the game is
  started anyway, as the check could be wrong on some IOS. Refusing the
  start with a message is the next step once Wii reports show the check
  is reliable.
- **Emulated NANDs on a USB drive aren't supported by the channel start.**
  The folder lookup and d2x's emulation set-up both take only the SD card.
- **A game that reloads IOS itself** loses both its identity and d2x's
  emulation: the channel start doesn't install the disc start's reload
  block. None of the 14 test games does this.
- **ELF programs** (Dr. Mario Online Rx, Fluidity) rely on the game's own
  start-up clearing its memory; only the first segment's zero-filled tail
  is recorded. Both start, but an ELF needing more from the loader would
  not.

## Who does what

- **RiftWii maintainers (this branch):** the test runs above, Dolphin's
  `ES_Identify`, and the research on decompilation projects.
- **Jack (@JackW6809):** the four Wii Remotes in channels.
- **Cyan (@cyanpurr):** the emulated NAND side, if you'd like it: you
  wrote it (#220).
- **grantendo:** the GameCube pad's way back in N64 VC games, reviews, and the memory side.

Anything else you'd like to take, say so in the PR and it comes off this
list.

## Rules for this work

- Decompilation projects are read as references for how a game
  behaves, as the Wii Sports one was for the menu music. RiftWii's code
  stays its own: nothing copied, every reference credited in
  `NOTICE.md`.
- No Nintendo files, keys or WADs in the repository.
- Each fix is checked in Dolphin first, and the parts Dolphin can't run
  (d2x cIOS, the real NAND) are named as untested until a Wii confirms
  them.
