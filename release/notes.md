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
**RiftWii {{VERSION}}.** A fix for games that wouldn't start after copying a disc. Every RiftWii offers this update by itself (Settings > Check for a new version).

## What changed

- **Games start after Copy a game disc.** To copy a disc, RiftWii restarts itself under a cIOS. After that, some games (Super Mario Galaxy 2 among them) failed to start in the same session with "the apploader asked for a load outside RAM". RiftWii now sets up the memory a game's start-up code reads itself, as the Wii Menu does, so they start.

## What to test

1. **Copy a game disc, then start the copy straight away** without restarting RiftWii, Super Mario Galaxy 2 especially.

## Known issues

- **007 Everything or Nothing (GameCube)** goes back to RiftWii instead of starting its main game on some consoles. A problem report sent right after it lands back in RiftWii helps.
- **007 NightFire (GameCube):** on a real Wii the Wii Remote can stop at the first mission, and HOME with − stops working too. A problem report helps (turn the Wii off, start RiftWii again, then send it).
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **USB gamepads:** `MultiIn` 2 to 4 and pads that pack their sticks in their own way aren't supported yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
