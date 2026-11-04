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
**RiftWii {{VERSION}}.** The whole menu now looks and moves like the Wii's own: glossy buttons, a springy hover, page bands and a glint on Play. Also: Goblet of Fire no longer runs out of memory, a Wii Remote fix for Need for Speed Carbon (GameCube), Wii and GameCube views on Home, and a phone-style PIN keypad. Every RiftWii offers this update by itself (Settings > Check for a new version).

![Settings and its glossy cards in six themes]({{SCREENSHOTS}}2611-wii-look-themes.png)

## What changed

- **The menu in the Wii's own design.** Things you press (Settings' cards, the game page's tiles and Play, list rows, the prompts along the bottom, switches, keyboard keys) are now glossy like the Wii Menu's and the Disc Channel's buttons. Covers, frames and panels stay flat. Every theme gets the look in its own colours, with nothing to change in its folder.
- **A hover you can feel.** A tile or cover swells a little with a small spring when the pointer or the D-pad reaches it.
- **Bands on every page.** Settings, its lists and the game page have a title band at the top and a band at the bottom for the prompts, as in the Wii's System Settings.
- **Play catches the light** every few seconds while it's lit, and so does a message box's lit button.
- **Message boxes light the safe choice** as they open, so you can see what A will do. On questions that can't be undone (such as resetting all settings), that's Cancel.
- **The menu moves more like the Wii's:** a turning ring for every wait, smooth progress bars, buttons that dip when pressed, a softly breathing highlight and page arrows that invite you to the next page.
- **The HOME Menu:** the D-pad from Close goes to the right column, a pointer resting on screen no longer hides the D-pad's highlight, and long labels such as "Homebrew Channel" keep a margin.
- **Wii games and GameCube games in Show.** Two new views after All games on Home. Button 1 steps through them, and the last one used is remembered.
- **PIN keypad.** The parental controls PIN is typed on a keypad laid out like a phone's, with bigger keys, and an easy-to-guess PIN gets a word of warning.
- **Harry Potter and the Goblet of Fire (GameCube) no longer runs out of memory** at start (about 10 MB less needed). Checked in Dolphin with the release build's memory and menu music on.
- **Need for Speed Carbon (GameCube):** the Wii Remote should keep working past the second "press a button" screen. Not tried with the game yet.
- **Retro Rewind** no longer asks "Mod files are missing" about its Mission folder.
- **Wii U adapter:** after a failed launch, a GameCube controller on the adapter can answer RiftWii's waiting screens.
- **vWii:** the NAND permission check RiftWii opens to find your cIOS closes again afterwards.

![Settings, before and after]({{SCREENSHOTS}}2611-wii-look-settings.gif)

![The game page, before and after]({{SCREENSHOTS}}2611-wii-look-game.gif)

## What to test

1. **The new look** in your theme: Settings, a game's page, the HOME Menu. Does anything look cut off or out of place on your TV?
2. **Harry Potter and the Goblet of Fire** (GameCube), with menu music on.
3. **Need for Speed Carbon** (GameCube) with a Wii Remote, past the second "press a button" screen.
4. **Show > Wii games / GameCube games** on Home.
5. **Parental controls:** set and type a PIN on the new keypad.

## Known issues

- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet (the Switch Pro is built in).
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
