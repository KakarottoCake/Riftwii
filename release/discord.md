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

What's new:

* Games get back a lot of the memory RiftWii used inside them (thanks grantendo and JackW), and mods load faster
* Parental controls can lock or hide the Homebrew Channel and Priiloader buttons, and lock Settings
* After the next update, RiftWii restarts straight into the new version
* Mod tiles get their own 3D covers and boxes
* Code mods: Extras and Crash logs rows on the Mods page
* Wii U Pro Controller: Mario Kart Wii CC cheats work without opening HOME first

To test, please:

1. Big mods from SD and USB: faster? Any freezes or missing files?
2. Saves in a mod with its own save folder, after a restart
3. Code builds (Project+, REX, PM), especially from sd.raw
4. Parental controls: set a PIN, try each HOME Menu buttons choice and the Settings lock
5. MKWii with a Pro Controller and CC cheats
6. GameCube adapter: pull the gray plug mid-game, rumble should stop and controllers keep working

Sending logs (you don't need to take out your SD card): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![What's new](docs/screenshots/2611-128-whats-new.png)
![HOME Menu with parental controls](docs/screenshots/2611-128-home-menu-parental.png)
