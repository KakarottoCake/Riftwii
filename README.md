<p align="center">
  <img src="docs/images/banner.png" alt="RiftWii" width="640">
</p>

<p align="center">
  <b>A mod loader for the Wii.</b> Play Riivolution-format mods from the disc, a USB drive or the SD card.
  <br><br>
  <a href="https://github.com/KakarottoCake/Riftwii/releases/latest"><b>Download</b></a> ·
  <a href="docs/GUIDE.md">Guide</a> ·
  <a href="docs/GUIDE.md#troubleshooting">Troubleshooting</a> ·
  <a href="https://github.com/KakarottoCake/Riftwii/issues">Report a problem</a> ·
  <a href="https://discord.gg/gGsTdjjQaK">Discord</a>
</p>

<table>
  <tr>
    <td width="50%"><img src="docs/images/home.png" alt="Home"><br><p align="center">Your games, with covers</p></td>
    <td width="50%"><img src="docs/images/game.png" alt="A game's page"><br><p align="center">Each game's own settings</p></td>
  </tr>
  <tr>
    <td><img src="docs/images/mods.png" alt="Mods"><br><p align="center">Turn mod packs on and pick their options</p></td>
    <td><img src="docs/images/cheats.png" alt="Cheats"><br><p align="center">Cheats, downloaded for you</p></td>
  </tr>
  <tr>
    <td><img src="docs/images/settings.png" alt="Settings"><br><p align="center">Settings</p></td>
    <td><img src="docs/images/homemenu.png" alt="HOME Menu"><br><p align="center">A HOME Menu, like the Wii's</p></td>
  </tr>
</table>

## Features

- **Mods, no patching.** Riivolution-format packs (the same XML and folders) load into the game as it starts. Your game files are never changed.
- **Play from anywhere.** The disc, a USB drive (FAT32, NTFS or formatted as WBFS) or the SD card, as WBFS, ISO or Dolphin's RVZ.
- **Big mods work.** Pulsar and CT-CODE packs such as CTGP and other Mario Kart Wii distributions, plus packs over the network from a PC (RiiFS), and Gecko code builds like Project+ and REX without their launchers.
- **Saves kept apart.** Modded saves can live on the SD card, away from your Wii saves.
- **Per game settings.** Cheats (downloaded for you), picture width, deflicker, borders, video mode (480p, PAL 60), game language and cIOS.
- **Online play.** Wiimmfi, WiiLink WFC, AltWFC or your own server.
- **Feels like a Wii.** Covers and names from GameTDB, favourites, recently played, an A to Z jump, a HOME Menu and five languages.
- **Any controller.** Wii Remote, Classic Controller, GameCube controller, the GameCube adapter for Wii U, and USB pads through fakemote.
- **A Wii Menu channel**, on a Wii or a Wii U, and **updates** from inside RiftWii (Stable or Beta).
- **Experimental:** burned Wii discs, in the Wii's drive or a USB DVD drive.

## Getting started

You need a Wii (or a Wii U in Wii mode) with the Homebrew Channel and a FAT32 SD card. Games on USB or SD need a [d2x cIOS](docs/GUIDE.md#what-you-need).

1. Download the zip from [Releases](https://github.com/KakarottoCake/Riftwii/releases/latest).
2. Copy its `sd-card` folder to your SD card (and `usb-drive` to your USB drive, if you use one).
3. Put mod packs in `sd:/riivolution` and games in `wbfs` or `games`.
4. Start RiftWii from the Homebrew Channel.

Questions, test builds and help are on the [RiftWii Discord](https://discord.gg/gGsTdjjQaK). The [guide](docs/GUIDE.md) covers everything else: each screen, online play, the GameCube adapter, writing packs, and what to send when something goes wrong.

## About AI assistance

Yes, RiftWii was written with AI assistance, and I won't pretend
otherwise. But it was not vibecoded. Every step was supervised closely,
because even the best models make terrible calls when you just throw
them at a project and walk away. Left alone, they suggested "fixes" like
patching discs at random based on assembly fingerprints to make a
problem go away, and seemed to think that was fine. I can't imagine what
a truly vibecoded version of this would look like. The design decisions,
the testing on real hardware and the refusal to ship shortcuts like that
are mine.

This is a hobby project. As a kid I wanted to play mods straight from a
USB drive, and Riivolution never allowed it; its developer was firmly
against USB loading. RiftWii exists to finally get past that.

## Licence

GPL-3.0-or-later ([LICENSE](LICENSE)). RiftWii was written from scratch and contains no Riivolution code; third-party notices are in [NOTICE.md](NOTICE.md). Developers: see [docs/DEVELOPING.md](docs/DEVELOPING.md) and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
