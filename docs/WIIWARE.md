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
by RiftWii's test build (`channelboot <title>` in `autorun.txt`).

| Game | Title | Direct | RiftWii in Dolphin | Real Wii |
| --- | --- | --- | --- | --- |
| WarioWare: D.I.Y. Showcase | 00010001-WA4E | Runs, makes its save | All files read; stops at "An error occurred while accessing Wii system memory" (see below) | Not yet |

## Findings

### Dolphin refuses `ES_Identify`, so a channel can't open its save

RiftWii starts a channel the way a loader does: it asks IOS to run
RiftWii as that title (`ES_Identify`) and jumps to the game's program.
Dolphin answers -1017 to both the installed ticket and the synthetic
one. With `debug_off = identify` the launch carries on as title
00000000-00000000, and WarioWare stops on the "Wii system memory" error
because it can't open its own save folder. Until this works in Dolphin,
no channel can be tested there past its first save access.

Next: find out from Dolphin's own source (GPL, an allowed reference)
what its ES needs before it accepts `ES_Identify`, or which other way
of starting a title it supports.

## Who does what

- **RiftWii maintainers (this branch):** the test runs above, Dolphin's
  `ES_Identify`, and the research on decompilation projects.
- **Jack:** the four Wii Remotes in channels. Anything else you'd like
  to take, say so in the PR and it comes off this list.

## Rules for this work

- Decompilation projects are read as references for how a game
  behaves, as the Wii Sports one was for the menu music. RiftWii's code
  stays its own: nothing copied, every reference credited in
  `NOTICE.md`.
- No Nintendo files, keys or WADs in the repository.
- Each fix is checked in Dolphin first, and the parts Dolphin can't run
  (d2x cIOS, the real NAND) are named as untested until a Wii confirms
  them.
