<!--
The GitHub release notes for the next release. The release workflow
(.github/workflows/release.yml) fills in the {{...}} names and drops these
comments; docs/DEVELOPING.md (Releasing) says how to run it.

  {{VERSION}}      2610-500            {{TAG}}        v2610-500
  {{DOL_SHA256}}   the DOL's SHA-256   {{DOL_BYTES}}  the DOL's size
  {{SCREENSHOTS}}  this tag's docs/screenshots/ on raw.githubusercontent.com,
                   so ![...]({{SCREENSHOTS}}2610-500-menu.png) shows a
                   screenshot committed before the release

Only what a player notices goes in: leave out CI and workflows, review
bots (CodeRabbit), tools/, tests, docs and refactors (docs/RELEASING.md).

Rewrite the intro, What changed, What to test and Known issues for every
release, then delete the line below: the workflow refuses notes that still
have it, or that haven't changed since the last release's tag.
-->
**RiftWii {{VERSION}}.** The Wii Menu's own sounds and music, a GameCube-pad way out of Wii games, GameCube memory cards in three more games, Nintendont saves, GameHacking.org cheats, fixes for the RiftWii channel on a Wii U, and a smaller download. Every RiftWii offers this update by itself (Settings > Check for a new version).

![The Wii Menu's sounds and music in Settings > Menu]({{SCREENSHOTS}}2610-720-wii-menu-sounds.png)
![Bringing Nintendont's saves over]({{SCREENSHOTS}}2610-690-nintendont-saves.png)
![Cheats from GameHacking.org]({{SCREENSHOTS}}2610-690-gamehacking-cheats.png)

## What changed

### New

- **The Wii Menu's own sounds and music,** Settings > Menu > Wii Menu sounds and Wii Menu music (both off until you turn them on). RiftWii reads them from your Wii each time it starts and keeps them in memory only; nothing of Nintendo's is in the download. A theme's own sounds and music still come first, and anything missing falls back to RiftWii's own. Checked in Dolphin with Wii Menu 4.3U; not tested on a real Wii or a vWii yet.
- **Leave a Wii game from a GameCube controller:** with the GameCube adapter on, hold L, R, Z and Start for two seconds. It goes back the way the HOME Menu's Wii Menu button does (to RiftWii, or the Wii Menu if you set that), and waits for a save in progress to finish first. Tested on a Wii U.
- **Home starts on your last game** (Settings: Home starts on, Last game or First page) once its drive is read, unless you've already pressed a button.

### Home

- **GameCube controls on Home,** L/R turn pages, Z opens Settings, B opens Search; with Home sorted A to Z, hold L and press Left or Right to jump by letter. Not tested on a real Wii yet.
- **A tidier Home,** Nintendont's own channel, USB Loader GX's forwarder and packed homebrew loaders no longer show as games; a failed Settings save puts the old value back.

### GameCube

- **Memory cards in Need for Speed: Underground 2, Resident Evil 2 (USA) and Fire Emblem: Path of Radiance,** RiftWii's card finder no longer picks the wrong table, and accepts RE2's older SDK. Tested on a real Wii: NFSU2 profile saved and loaded, RE2 sees Slot A, Fire Emblem PAL sees the card.
- **Memory cards and saves survive errors,** GameCube cards survive failed reads instead of being reformatted, and saves are kept when the SD card's status check fails.
- **Bring Nintendont's GameCube saves over,** all of them or chosen ones in Settings.
- **Cleaner GameCube sound,** games stop their sound before going back so the way back no longer buzzes (not tested on a real Wii yet), and sound no longer breaks up while games read the disc (Melee, Sunshine cut-scenes).
- **A sideways Wii Remote in GameCube games on a Wii U (vWii),** e.g. Mario Kart: Double Dash!!. Checked in Dolphin; not tested on a Wii U yet.
- **The Wii U GameCube adapter in more places,** it now works in Wii games from RVZ images (not tested on a real Wii yet), and launches from the RiftWii channel keep the adapter.
- **Splinter Cell: Double Agent no longer crashes,** its memory no longer wraps.

### Online and USB

- **Cheats even when rc24 has none,** RiftWii falls back to GameHacking.org's archive. Fluidity (WiiWare) no longer goes black with a cheat on.
- **Automatic launches keep trying,** they try every installed d2x slot after the preferred choices.

### The RiftWii channel and WiiWare

- **A channel update that won't run no longer locks you out.** On some Wii U consoles the channel installer went black, and RiftWii refused to start until the channel was updated. Now, if the installer never comes back, the next start says so and offers **Not now**. The installer also logs before it switches cIOS, only enters a slot it has checked holds a d2x (or asks first when it can't check), and gives a reason on screen instead of hanging. Not tested on an affected Wii U yet: please try it if your installer went black.
- **A game's own Reset (HOME Menu > Reset) reaches the RiftWii channel on a Wii U** and keeps the GameCube adapter on. Tested on a Wii U.
- **WiiWare from an emulated NAND on the SD card (vWii):** RiftWii lets go of the SD card before d2x mounts it, the cause of the "mount -101" failures. Not tested on a Wii U yet.
- **WiiWare that quit straight back to RiftWii** (WarioWare: D.I.Y. Showcase): an educated guess at what the game checked at start-up, with log lines that tell whether it was right. Please send a problem report from any WiiWare game that quits.

### Everything else

- **Taking the SD card out and putting it back** no longer leaves RiftWii failing every read: it notices, says so, and reads the card again (a different card too). Games and Settings wait until the card is back. Not tested on a real Wii yet.
- **The disc stops spinning on power-off to standby (orange light).** Tested on a real Wii.
- **Homebrew apps come back to RiftWii when they exit.**
- **Smaller, lighter, and calmer with broken files,** the download is smaller and RiftWii has 524 KiB more free memory (DOL about 960 KiB smaller); broken or unusual files (FAT32, NTFS, RVZ, banners, downloads, GameCube discs) are refused cleanly instead of crashing or misbehaving.

## What to test

1. **A GameCube game that said no memory card:** NFSU2, RE2, Fire Emblem, or any other that didn't see the card.
2. **The GameCube adapter in a Wii RVZ game:** try the front and back USB ports.
3. **A sideways Wii Remote in a GameCube game on a Wii U.**
4. **L/R/Z/B on Home with a GameCube controller,** including letter jumps with Home sorted A to Z.
5. **Bring Nintendont's saves over,** all and chosen ones.
6. **Power off with a disc in** and check the disc stops spinning.
7. **Wii Menu sounds and music** (Settings > Menu): turn them on, move around Home and Settings. Wii U (vWii) owners especially, and other regions than USA.
8. **Hold L, R, Z and Start** on a GameCube controller in a Wii game (adapter on), and during a save.
9. **Wii U owners whose channel installer went black:** update RiftWii, then try the channel update again; send a problem report either way.
10. **WiiWare** from the Wii or an emulated NAND, and **taking the SD card out and back in** on Home.

## Known issues

- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **The GameCube memory card in other regions' copies of Resident Evil 2 (PAL, Japan) isn't covered yet.**
- **The RiftWii channel freezes on some vWii consoles** (#228). Channel 14, which RiftWii offers on a vWii, shows the step it stops at; please send what the screen says.
- **The channel installer goes black on some Wii U consoles** (#317). RiftWii now offers Not now instead of locking you out, but the cause isn't confirmed: `sd:/riftwii/channel.log` in a problem report helps.
- **The Wii U Pro Controller's stick can stop, then drift, in the menu** after opening a game's page (#239).
- **Mario Party 7 on a real Wii** loses its sound at Press START, then stops taking input (#199).
- **Zelda Collector's Edition may show "No Signal"** at 480p on a real Wii (seen once; Dolphin doesn't show it).
- **Pokémon Channel** stops on The Pokémon Company logo (#194).
- **Cars 2 is cursed:** it goes black if RiftWii changes anything in it. RiftWii warns before you play it (and Toy Story 3 and Disney Infinity, which share its engine).
- **Ghostbusters from an RVZ** can still freeze in the hotel: play it from an ISO or WBFS.

Everything new since 2.7.0: [docs/CHANGES_2610.md](https://github.com/KakarottoCake/Riftwii/blob/main/docs/CHANGES_2610.md).

## Download

Grab `riftwii-{{TAG}}.zip` below. `riftwii.dol` is the same build as the zip's `boot.dol` (SHA256 {{DOL_SHA256}}, {{DOL_BYTES}} bytes). `riftwii-update.pack` is what the in-app update reads (the plugins, the themes and the channel installer); you don't need to download it.

If something goes wrong, use Settings > Help > Send a problem report and send the link (you don't need to take out your SD card), or open an issue with `sd:/riftwii/session.log`, `sd:/riftwii/boot.log`, your Wii model, your Menu IOS and your cIOS.

## License and source

RiftWii is free software under the GNU General Public License, version 3 or later, with no warranty. Its source is this repository at the tag `{{TAG}}`; the zip carries `LICENSE.txt`, `NOTICE.md` and `SOURCE.txt`. [NOTICE.md](https://github.com/KakarottoCake/Riftwii/blob/main/NOTICE.md) credits every project RiftWii contains or follows, with each piece's origin and license.
