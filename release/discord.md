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

* Home: a Layout button (top left) and a Show chooser (bottom left), and help when a list is empty
* WiiWare and VC straight from .wad files in a wiiware folder, no install step
* A likely fix for Retro Rewind's error 50330 online
* Asks before playing when mod files are missing
* Banner music and disc art on game pages (Settings, off at first)
* A Save location page, and "Use default options for this game"
* Pictures for emulator games in Channels, and a picker to choose one
* Clearer error messages

To test, please:

1. Put a WiiWare or VC .wad in sd:/wiiware: does it show, start, save, and start again without unpacking?
2. Retro Rewind online from the disc, after the menu was online: does it connect now?
3. The Layout and Show buttons on Home: anything confusing?
4. A mod with a file missing: do you get the question, and does Play anyway work?

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![The Layout picker](docs/screenshots/2610-layout-picker.png)
![Disc art on a game's page](docs/screenshots/2610-disc-art.png)
