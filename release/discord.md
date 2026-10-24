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

The Wii Menu's own sounds, a GameCube-pad way out of Wii games, and Wii U channel fixes:

* Wii Menu sounds and music: Settings > Menu (read from your Wii, off until you turn them on)
* Hold L+R+Z+Start on a GameCube controller to leave a Wii game (adapter on; waits for saves)
* Wii U: a channel installer that goes black no longer locks you out (Not now), and a game's Reset reaches the channel
* WiiWare: SD mount fix for emulated NAND on vWii, and a guess for games that quit straight back
* Pull the SD card and put it back: RiftWii reads it again
* Home starts on your last game

To test, please:

1. Wii Menu sounds and music on, especially on a Wii U or a non-USA Wii
2. L+R+Z+Start in a Wii game, and during a save
3. Wii U owners whose installer went black: try the channel update again
4. WiiWare games
5. SD card out and back in on Home

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![Wii Menu sounds and music](docs/screenshots/2610-720-wii-menu-sounds.png)
