<!--
The GitHub release notes for the next release. The release workflow
(.github/workflows/release.yml) fills in the {{...}} names and drops these
comments; docs/DEVELOPING.md (Releasing) says how to run it.

  {{VERSION}}      2610-500            {{TAG}}        v2610-500
  {{DOL_SHA256}}   the DOL's SHA-256   {{DOL_BYTES}}  the DOL's size
  {{SCREENSHOTS}}  this tag's docs/screenshots/ on raw.githubusercontent.com,
                   so ![...]({{SCREENSHOTS}}2610-500-menu.png) shows a
                   screenshot committed before the release

Only what a player notices goes in: leave out CI and workflows, review
bots (CodeRabbit), tools/, tests, docs and refactors (docs/RELEASING.md).

Rewrite the intro, What changed, What to test and Known issues for every
release, then delete the line below: the workflow refuses notes that still
have it, or that haven't changed since the last release's tag.
-->
**RiftWii {{VERSION}}.** GameCube games can be 16:9 again, and a full SD card no longer leaves you stuck: RiftWii tells you when the card is full, and the game page always lets you leave or play. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Luigi's Mansion in 16:9]({{SCREENSHOTS}}2611-179-gamecube-16x9.png)

## What changed

### GameCube games

- **16:9 is back.** A GameCube game's page has its 16:9 row again. Default keeps the game's own picture unless Settings > Picture > Aspect ratio is 16:9. Super Mario Sunshine's row says 16:9 isn't available for it yet. Last time, a real Wii showed Super Mario Eclipse with inverted colors in 16:9. That never happened in Dolphin, and the way it now hooks the game is the one that drew it right on a real Wii in October. It hasn't been tried on a real Wii since, so reports are welcome.

### When the SD card is full

- **RiftWii says when the SD card is full**, or can't be written to (locked, or read-only), with a message on Home that says what stops working and what to do. Before, a full card only left lines in the log.
- **The game page no longer traps you on a full card.** Back always leaves (it says your choices couldn't be saved), and Play asks "Play anyway?" and starts the game with the choices on screen. Before, neither worked and the Wii had to be switched off.

![The SD card is full]({{SCREENSHOTS}}2611-179-sd-full.png)

### Other fixes

- **USB and SD games no longer wait on a broken or missing disc drive.** The launch used to ask the real disc drive a question first, and a broken drive hung there for 15 seconds before the launch failed.
- **The disc slot light is off by default**, and never blinks while a cIOS is running. Its blinking could make that same disc drive question hang on some Wiis. If you turned it on before, it stays on.
- **Long cheat notes scroll** instead of being cut off, on the Cheats page and in the value keypad, so a controller remapper's whole button list can be read.
- **Settings > Games:** the Wii saves, EmuNAND folder and "keeps" options can be switched again. Each one was stuck on its second value.
- **CTGP-R with Mario Kart Wii on the SD card** now stops at once with a clear message (use a USB drive or the disc), instead of a black screen.

## What to test

1. **A GameCube game in 16:9** on a real Wii (Settings > Picture > Aspect ratio, or the game's own 16:9 row): are the colors right? Super Mario Eclipse especially.
2. **A full SD card:** does Home say so, and can you still leave a game's page and play?
3. **A USB or SD game** on a Wii whose disc drive is broken or empty: does it start without the 15-second wait?
4. **Settings > Games** options: do they switch both ways?

## Known issues

- **GameCube 16:9** hasn't been tried on a real Wii since its fix.
- **CTGP-R online:** a whole race and a Time Trial upload haven't been tried from RiftWii yet.
- **Super Smash Bros. Brawl's Reset** still loses the GameCube adapter on an SD card over 2 GB.
- **On one tester's console, a GameCube game's crash screen didn't go back to RiftWii.** It does in Dolphin. A problem report from that screen helps.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet without patches.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colors.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
- **xdelta patches that really compress a part** are still refused.
