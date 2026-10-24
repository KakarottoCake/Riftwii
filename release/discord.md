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

GameCube memory cards in three more games, Nintendont saves, and GameHacking cheats:

* Memory cards in NFSU2, Resident Evil 2 USA and Fire Emblem: Path of Radiance (tested on a real Wii)
* Bring Nintendont's GameCube saves over, all or chosen ones
* Cheats fall back to GameHacking.org; Fluidity no longer goes black with a cheat
* Disc stops spinning on power-off to standby; GameCube sound no longer breaks up or buzzes on the way back (buzz fix not Wii-tested)
* Wii U GameCube adapter in Wii RVZ games, and sideways Wii Remote in GameCube games on Wii U (both not Wii-tested yet)
* GameCube controls on Home (L/R pages, Z Settings, B Search, not Wii-tested), tidier Home, and homebrew apps return to RiftWii

To test, please:

1. A GameCube game that said no memory card (NFSU2, RE2, Fire Emblem)
2. GameCube adapter in a Wii RVZ game, front and back USB ports
3. Sideways Wii Remote in a GameCube game on a Wii U
4. L/R/Z/B on Home with a GameCube controller
5. Bring Nintendont's saves over
6. Power off with a disc in and check it stops spinning

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Bringing Nintendont's saves over](docs/screenshots/2610-690-nintendont-saves.png)
![Cheats from GameHacking.org](docs/screenshots/2610-690-gamehacking-cheats.png)
