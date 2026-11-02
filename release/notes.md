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
**RiftWii {{VERSION}}.** EA's 007 GameCube games: Agent Under Fire and Everything or Nothing now start, and the Wii Remote works in NightFire's missions and Everything or Nothing. USB controllers such as the DualShock 3 and 4 now work without a file on the SD card. Every RiftWii offers this update by itself (Settings > Check for a new version).

![007 Agent Under Fire's main menu]({{SCREENSHOTS}}2610-007-agent-under-fire.png)

![007 Everything or Nothing]({{SCREENSHOTS}}2610-007-everything-or-nothing.png)

![007 NightFire's first mission]({{SCREENSHOTS}}2610-007-nightfire.png)

## What changed

- **007 Agent Under Fire (GameCube) starts.** The game loads its parts from the disc, and each part begins with two short setup steps that RiftWii used to refuse to patch around. It now takes over just after them.
- **007 Everything or Nothing (GameCube) starts.** Its start-up program stopped on a black screen. It switches to the main game by restarting the console into it, an older way of doing that which RiftWii now recognises: the game goes back through RiftWii for a moment, which starts it again straight into the main game.
- **The Wii Remote works where a GameCube game keeps checking an empty controller port.** Some games ask the empty port 1 again and again whether a controller is there, and RiftWii used to leave that port to a GameCube controller that never came, so the Wii Remote did nothing. Everything or Nothing's menus and NightFire's missions take the Wii Remote now. A GameCube controller plugged in later still gets its port back.
- **USB controllers work out of the box.** RiftWii now carries the layouts from Nintendont's controller collection, about 40 pads: the DualShock 3, both DualShock 4 models, the DualSense and DualSense Edge, Logitech's F310 and RumblePad 2, the Nvidia Shield controller, and several 8BitDo, Mayflash, Hori, Thrustmaster and arcade-stick models. Plug the pad in before starting a game. A layout file of your own on the SD card (`sd:/riftwii/controllers/VID_PID.ini`) still comes first.

## What to test

1. **007 Agent Under Fire, Everything or Nothing and NightFire** with a Wii Remote: start a mission, then try the menus and the pause menu.
2. **Everything or Nothing's start**: it should go back to RiftWii for a moment and then start the main game by itself.
3. **A USB controller with no file on the SD card**, a DualShock 3 or 4 especially: does it move and press buttons in a GameCube game?

## Known issues

- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours (dark red and green instead of gold and silver). The game itself and its other scenes look right.
- **The DualShock 3 over USB** hasn't been confirmed to work: RiftWii sets it up, but no button presses have been seen from one yet.
- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
