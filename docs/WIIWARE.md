# WiiWare and Virtual Console: making them work every time

A working page for the effort to make WiiWare and Virtual Console games
start, save and return as they do from the Wii Menu. It keeps the plan,
the test results, and who is doing what. It's updated as the work goes.

## The goal

Every WiiWare and Virtual Console game a player has, on the Wii itself,
in an emulated NAND on the SD card, or moved to the SD card by the Wii
Menu, starts from RiftWii, saves, and goes back to RiftWii or the Wii
Menu.

## Open reports

| Report | What happens |
| --- | --- |
| Super Mario 64 (VC, N64) | Doesn't start (problem report from a player). |
| Cruis'n USA (VC, N64) | Same as Super Mario 64. |
| WarioWare: D.I.Y. Showcase on the Wii itself | Stopped after the cIOS line in `boot.log`; every start after it was a black screen until the Wii was switched off. #342 added log lines for each step and the clean-up at the next start. |
| Four Wii Remotes in some channels | Jack's work. |

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

All 14 reach the same screen through RiftWii as when Dolphin starts them
itself (2026-10-26, branch at 209b308c). In WarioWare, Dolphin's log
shows the game asking ES for its title and data folder and reading its
own content through ES, as it does when started directly. Not covered
yet: an emulated NAND (d2x's emulation can't run in Dolphin), channels
the Wii Menu moved to the SD card, saving, and going back to RiftWii.

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
request and runs the disc steps, marking the title as a channel. It's
written and checked to apply; building it is waiting for the maintainer
(it builds Dolphin from source, which needs their go-ahead).

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

## Who does what

- **RiftWii maintainers (this branch):** the test runs above, Dolphin's
  `ES_Identify`, and the research on decompilation projects.
- **Jack (@JackW6809):** the four Wii Remotes in channels.
- **Cyan (@cyanpurr):** the emulated NAND side, if you'd like it: you
  wrote it (#220).
- **grantendo:** reviews, and the memory side.

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
