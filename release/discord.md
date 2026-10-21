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

GameCube colors, saves, and sound:

* Mario Party 4 and Sonic Gems get their true colors in Wii mode (checked in Dolphin)
* A memory card copied from elsewhere no longer reads as damaged (checked in Dolphin)
* Melee, Mario Party 4 and 5, Sonic Mega Collection, Star Fox Assault, Beyond Good and Evil and Paper Mario TTYD should play their sound right on a Wii (not tested on one yet)
* A game with an unusual disc ID finds its old card again
* Logging for the four-Remote freeze (#138): session.log tracks the Remote stack and memory, no behavior change
* A pack changing one file over 128 times is refused with the file named

To test, please:

1. Saw the freeze with four Remotes? Connect four again; if it freezes, turn off, start RiftWii and send a problem report
2. Mario Party 4 / Sonic Gems on a Wii: colors and sound?
3. Melee and the other listed games on a Wii: sound?
4. A memory card copied from elsewhere: does it load?

Sending logs (you don't need to take out your SD card anymore): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.
