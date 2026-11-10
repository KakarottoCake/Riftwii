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
**RiftWii {{VERSION}}.** A big one for GameCube games: mod packs that change a game's code now start, a GameCube controller can open a Wii game's HOME Menu, and Paper Mario: The Thousand-Year Door no longer crashes partway in. CTGP Revolution starts without the disc, and there's 240p for CRT TVs. Every RiftWii offers this update by itself (Settings > Check for a new version).

![What's new in this version]({{SCREENSHOTS}}2611-151-whats-new.png)

## What changed

### GameCube games

- **Paper Mario: The Thousand-Year Door no longer crashes partway into the game.** RiftWii now finishes the game's sound-memory copies on the same schedule a GameCube does. The game wasn't ready for them to finish sooner. Luigi's Mansion and Super Mario Eclipse needed the old timing, and they should still work, but none of the three has been tried on a real Wii yet.
- **Mod packs that change a game's code now start.** Before, a GameCube pack with memory patches or "shift" patches refused to start. Now they're applied as Dolphin applies them. A game that switches to another program on its disc (like Sonic Gems Collection) keeps its mods too. Savegame patches still can't work, because GameCube games save to memory cards.
- **Calibrate a GameCube controller** in Settings > Controllers. Turn the sticks and press the triggers, and a worn controller reaches the full range again in games. There's also a dead zone slider for stick drift.
- **Memory cards:**
  - A game can use a real memory card in slot B (original Wii only).
  - A new choice puts the card in slot B where a game looks for slot A.
  - Settings > Games > GameCube memory card sets the card for every game left on Default.
- **Volume boost** (+3 dB or +6 dB) on a GameCube game's page, for games that are too quiet.
- **Old games like Luigi's Mansion no longer ask about progressive scan** every time when 480p is on.
- **GameCube games follow the Wii's Screen Position** (Wii Settings > Screen), as Wii games do.
- **Mario Kart Arcade GP 2** boots to its title screen. A on the GameCube controller in port 1 puts in a coin, and Start starts. It has no card reader or camera on a Wii, so it shows a notice about cards.
- **Patched games with their own code handler** (Phantasy Star Online with Sylverant's patches) no longer crash on the first frame.
- RiftWii's part inside a GameCube game takes 4.6 KB less of its memory.

### Wii games and controllers

- **A GameCube controller can open the HOME Menu** in Wii games: hold L + R + Z + Start for two seconds. The stick moves the pointer, A selects and Start closes it. In a game where that can't work, the same buttons take you back to RiftWii as before. Thanks grantendo, who tried it on a console in Mario Kart Wii, Naruto Clash of Ninja Revolution 3, Brawl and Cruis'n USA.
- **CTGP Revolution starts without the Mario Kart Wii disc**, from USB, SD or the disc, using your own CTGP files on the SD card. It's offline only: online play and LAN are always off.
- **Settings > Games > Leaving a game** has a new **HOME only** choice, so a button combo like L + R + Z + Start can't end a game by accident.
- **Cars 2** works with the GameCube adapter, screenshots, mods and cheats, and the "cursed" warnings for Cars 2, Toy Story 3 and Disney Infinity are gone.

### Code builds (Project+, REX)

- Starting RiftWii from the Homebrew Channel no longer sends REX to a black screen or Project+ to its controller setup screen (thanks grantendo).
- On a Wii U, codes that write to memory card slot B are switched off. REX carries one, and it froze on a black screen.

### Picture and menu

- **240p output for CRT TVs**: a new 240p choice in Settings > Picture > Video mode and on each game's page. It needs a CRT to really check, so reports are welcome.
- The Show and Layout pop-ups center each card's picture and name.
- A card reader with several slots: RiftWii now stops with a message instead of letting the game read a different slot than the menu did.

![Calibrate a GameCube controller]({{SCREENSHOTS}}2611-151-calibrate.png)

## What to test

1. **Paper Mario: The Thousand-Year Door**: play past the point where it crashed before. Also Luigi's Mansion and Super Mario Eclipse for a while.
2. **GameCube mod packs** with code changes (Super Mario Sunshine mods, Eclipse and others): do they start and play?
3. **HOME Menu with a GameCube controller** in Wii games: hold L + R + Z + Start with no Wii Remote.
4. **CTGP Revolution** without the disc, from USB and from SD.
5. **Calibrate a GameCube controller**, then try a game. Does the stick feel right?
6. **240p** on a CRT TV, Wii and GameCube games.
7. **Memory card in slot B** on an original Wii, and **Volume boost**.

## Known issues

- **On one tester's console, a GameCube game's crash screen didn't go back to RiftWii.** It does in Dolphin. A problem report from that screen helps.
- **The emulated NAND on USB hasn't been tried with d2x on a real Wii yet.** If a game won't start with it, the problem report says why.
- **Phantasy Star Online Episode I & II Plus** doesn't start yet without patches.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colors.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
