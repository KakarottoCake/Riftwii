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
**RiftWii {{VERSION}}.** GameCube memory cards in three more games, Nintendont saves, GameHacking.org cheats, a disc that stops on standby, and a smaller download. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Bringing Nintendont's saves over]({{SCREENSHOTS}}2610-690-nintendont-saves.png)
![Cheats from GameHacking.org]({{SCREENSHOTS}}2610-690-gamehacking-cheats.png)

## What changed

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

### Everything else

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

## Known issues

- **GameCube is a trial:** some games may not start or may stop. Send a problem report from Settings > Help when one doesn't.
- **The GameCube memory card in other regions' copies of Resident Evil 2 (PAL, Japan) isn't covered yet.**
- **The RiftWii channel freezes on some vWii consoles** (#228). Channel 14, which RiftWii offers on a vWii, shows the step it stops at; please send what the screen says.
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
