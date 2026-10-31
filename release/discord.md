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

New:

* No more freeze with a 4th Wii Remote
* USB gamepads work inside games now, and wired Xbox 360 pads work with no config file
* A cleaner menu font
* Your own covers (a PNG in sd:/riftwii/covers named after the game ID), and GameTDB's second cover for games that have one
* Home's shortcuts on every controller
* Fixes for EA's 007 GameCube games, 007 Quantum of Solace and Prince of Persia: The Forgotten Sands
* Restart with a USB drive no longer crashes, and homebrew started from RiftWii sees the GameCube adapter again

To test, please:

1. Four Wii Remotes in the menu, at start and one at a time
2. A USB pad or wired Xbox 360 pad in a GameCube game (plug it in before starting)
3. Your own cover, and the GameTDB cover row on Brawl or Mario Kart Wii

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![What's new](docs/screenshots/2610-whatsnew-781.png)
![The GameTDB cover row](docs/screenshots/2610-gametdb-cover.png)
