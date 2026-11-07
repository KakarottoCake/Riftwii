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
**RiftWii {{VERSION}}.** The channel installer installs again, Wii Menu sounds and music work with a Wii Menu theme installed, and the themes download at half the size. Every RiftWii offers this update by itself (Settings > Check for a new version).

## What changed

- **The channel installer works again.** In 2611-59 it could stop right after reading the cIOS slots with "NAND access was not restored" and install nothing. It now waits until the Wii is ready before putting things back, and tries again if the first try doesn't take. Not tried on a real Wii yet.
- **Wii Menu sounds and music with a Wii Menu theme.** A Wii Menu theme replaces one of the Wii Menu's files with a bigger one, and RiftWii refused it because it wasn't the size the Wii has on record. RiftWii now reads it at its real size. Not tried on a real Wii yet.
- **Themes download at half the size.** Bookshelf went from 2.3 MB to 1.1 MB and Indigo from 0.3 MB to 0.2 MB, with no change you can see on a TV. The update download after a new version gets smaller too, and so does the memory RiftWii sets aside for it. The themes on the themes site got the same treatment.

## What to test

1. **The channel installer:** start "RiftWii Channel Installer" from the Homebrew Channel and install or update the channel. Does it finish and go back to RiftWii?
2. **If you have a Wii Menu theme installed:** turn on Wii Menu sounds and Wii Menu music in Settings. Do you hear the Wii Menu's sounds and music?
3. **Themes:** does Bookshelf or Indigo look any different to you?

## Known issues

- **The emulated NAND on USB hasn't been tried with d2x on a real Wii yet.** If a game won't start with it, the problem report says why.
- **Everything or Nothing's THX and MGM logo movies** show in the wrong colours.
- **The DualShock 3 over USB** hasn't been confirmed to work yet.
- **The Wii U GamePad** works in the menu only when RiftWii runs as a Wii VC inject, and never in games.
