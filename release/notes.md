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
**RiftWii {{VERSION}}.** A fix for RiftWii restarting on every start after an update, and for losing hardware access after the channel installer. Every RiftWii offers this update by itself (Settings > Check for a new version).

## What changed

- **No more restart loop after an update.** After updating, RiftWii downloads its new themes and app files in the background. On a Wii with lots of games, covers and banners, that download could run out of memory, which restarted RiftWii, which started the download again, over and over. RiftWii now sets the memory for it aside right at start-up and downloads straight into it. If the memory isn't there, it skips the download for that start instead of restarting.
- **Hardware access stays on after the channel installer.** Coming back to RiftWii from "RiftWii Channel Installer" showed *Hardware access: no*, so RiftWii couldn't read the Wii Menu's sounds, the cIOS slots or the channels' names. The installer now keeps the Homebrew Channel's access through its IOS reloads. Not tried on a real Wii yet.

## What to test

1. **If RiftWii kept restarting for you after the last update**, does it start normally now and stay up?
2. **Run RiftWii Channel Installer** from the Homebrew Channel and let it go back to RiftWii. Then send a problem report (Settings > Help): its log should say *Hardware access: yes*.

## Known issues

- **The emulated NAND on USB hasn't been tried with d2x on a real Wii yet.** If a game won't start with it, the problem report says why.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
