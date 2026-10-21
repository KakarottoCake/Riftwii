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

Real GameCube memory cards, the Zelda bonus discs, and lots of GameCube fixes:

* Real GameCube memory cards (Wii with GameCube slots): copy saves from one, back up a whole card, or play with "Real card in slot A"
* The Zelda bonus discs start their games instead of going black
* HOME Menu > Reset in a Wii game with mods keeps the mods
* Start now: Mario Party 5/6/7, DDR Mario Mix, Pikmin 2 from an RVZ, Doshin the Giant, NBA Courtside 2002
* Fixed: Eternal Darkness's opening, Luigi's Mansion with progressive scan, Japanese text on a Western Wii, cards from another console, F-Zero GX's controller ports
* The update shows every step and the screen stays lit; Settings > Online > Download missing covers

Cards, bonus discs, Reset and F-Zero GX are checked in Dolphin only.

To test, please:

1. Zelda bonus discs: pick each game. On a real Wii at 480p, report any "No Signal" with boot.log and gc/trace.bin
2. HOME Menu > Reset with mods (Wii Party is a good one), and the console's Reset button
3. Real cards: copy from slot A, then B; back up a card; play on "Real card in slot A"
4. F-Zero GX with a GameCube controller and a Wii Remote; replug the controller mid-race
5. The other GameCube fixes, and Sunshine on a Wii U
6. The update itself, then Download missing covers

Sending logs (you don't need to take out your SD card anymore): in RiftWii go to Settings > Help > Send a problem report, scan the QR code or copy the link, and post it here with what you tested.

![A GameCube game's Memory Card page with the real card options](docs/screenshots/2610-587-real-memory-card.png)
![Settings, Online: Download missing covers](docs/screenshots/2610-587-missing-covers.png)
