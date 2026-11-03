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

New and fixed:

* Choose cover: pick any of GameTDB's covers for a game (or a mod's tile) in its settings
* WiiSX and Wii64 start now, from the Homebrew list and the Emulators view
* 007 NightFire: the Wii Remote and HOME should keep working in missions with no GameCube controller plugged in
* 007 Everything or Nothing from USB switches to its main game like it does from SD
* Games start after Copy a game disc (Super Mario Galaxy 2 and others)
* The channel installer no longer stops with "NAND access was not restored" from the Homebrew Channel

To test, please:

1. NightFire with a Wii Remote: play into the first mission. Does the Remote keep working?
2. Everything or Nothing from a USB drive
3. Choose cover on a couple of games, then Back to automatic
4. WiiSX or Wii64, and quitting back to RiftWii

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Choose cover](docs/screenshots/2610-choose-cover.png)
![The Choose cover row](docs/screenshots/2610-choose-cover-row.png)
