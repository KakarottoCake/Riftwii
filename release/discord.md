<!--
The message to the testers' Discord channel for the next release, posted
by the release workflow after the GitHub release is up. Discord's own
markdown: at most 2000 characters once filled in (the workflow counts).
-->
{{TESTER_ROLE}} RiftWii {{VERSION}} is out. {{RELEASE_URL}}

GameCube 16:9 and a few fixes:

* GameCube games can be 16:9 again (game page row, or Settings > Picture > Aspect ratio 16:9)
* RiftWii says when the SD card is full or can't be written
* A full SD card no longer traps you on a game page: Back leaves, Play asks "Play anyway?"
* USB and SD games no longer hang 15 s on a broken or missing disc drive
* Disc slot light off by default
* Long cheat notes scroll
* Settings > Games options switch again

Not tried on a real Wii yet: GameCube 16:9 since its fix (Super Mario Eclipse had wrong colors last time).

To test, please:

1. A GameCube game in 16:9, especially Super Mario Eclipse: are the colors right?
2. A full SD card: does Home say so, and can you leave a game page?
3. A USB or SD game on a Wii with a broken or empty disc drive

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![GameCube 16:9](docs/screenshots/2611-179-gamecube-16x9.png)
![The SD card is full](docs/screenshots/2611-179-sd-full.png)
