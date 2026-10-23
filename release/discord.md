<!--
The message to the testers' Discord channel for the next release, posted
by the release workflow after the GitHub release is up. Discord's own
markdown: at most 2000 characters once filled in (the workflow counts).

  {{TESTER_ROLE}}  pings the tester role (the DISCORD_TESTER_ROLE_ID variable)
  {{VERSION}}, {{TAG}}, {{RELEASE_URL}}  as in release/notes.md

A line that is only an image, ![what it shows](docs/screenshots/<file>.png),
is sent as an attachment under the message (up to 10, 10 MB in all)
instead of as text.

Only what a player notices goes in: leave out CI and workflows, review
bots (CodeRabbit), tools/, tests, docs and refactors (docs/RELEASING.md).

Rewrite it for every release and delete the line below.
-->
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

Wii channels, WiiWare and homebrew on Home, themes to download, and Retro Rewind online again:

* Wii channels and WiiWare on Home, with per-channel Game IOS and NAND choice
* Your homebrew apps on Home: press Y to the Homebrew view
* Settings > Theme > Get more themes
* Two-disc GameCube games: one tile, and the disc changes when the game asks
* Retro Rewind online works again (error 20911)
* Steadier GameCube controls (Double Dash), and USB games that d2x can't read stop with a message instead of crashing
* Settings: Network Test, and Games on the SD card (Don't look, for USB-only setups)

Channels, homebrew and two-disc games are checked in Dolphin only.

To test, please:

1. Start some Wii channels and WiiWare from Home
2. Start a homebrew app, from the Homebrew Channel and from the RiftWii channel
3. Retro Rewind online, then HOME Menu > Reset
4. A two-disc GameCube game up to the disc change
5. Double Dash or other GameCube games: do the controls feel right now?
6. A USB game on a Wii U

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Home with Wii channels and WiiWare](docs/screenshots/2610-615-wii-channels.png)
![The Homebrew view](docs/screenshots/2610-615-homebrew.png)
![Get more themes](docs/screenshots/2610-615-get-more-themes.png)
![What's new](docs/screenshots/2610-615-whats-new.png)
