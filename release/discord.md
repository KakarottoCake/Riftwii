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

GameCube, a lot of it:

* Paper Mario TTYD no longer crashes partway in
* GameCube mod packs that change code (memory and shift patches) now start
* GameCube controller opens the HOME Menu in Wii games: hold L+R+Z+Start (thanks grantendo)
* CTGP Revolution starts without the disc, offline only
* Calibrate a GameCube controller (Settings > Controllers)
* Real memory card in slot B, Volume boost, no more 480p question in old games
* 240p for CRT TVs
* Leaving a game: HOME only, so combos can't end a game
* Mario Kart Arcade GP 2 boots and takes a coin

To test, please:

1. TTYD past where it crashed, plus Luigi's Mansion and Eclipse
2. GameCube mods with code changes (Sunshine mods, Eclipse)
3. L+R+Z+Start HOME Menu with only a GameCube controller
4. CTGP without the disc, from USB and SD
5. Calibrate a controller, then play
6. 240p on a CRT
7. Slot B memory card on an original Wii

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![What's new](docs/screenshots/2611-151-whats-new.png)
![Calibrate a GameCube controller](docs/screenshots/2611-151-calibrate.png)
