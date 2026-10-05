# RiftWii documentation

For players, [GUIDE.md](GUIDE.md) covers installing and using
RiftWii, and [TESTING.md](TESTING.md) is the test plan for testers.
The other pages are for people working on it.

## Current

| Page | What it covers |
| --- | --- |
| [MAP.md](MAP.md) | Every file and folder, and where to start for each feature |
| [ARCHITECTURE.md](ARCHITECTURE.md) | How a launch works, which file owns what, the memory map, the files on the SD card |
| [DEVELOPING.md](DEVELOPING.md) | Building, tests, Dolphin runs, test scripts, releasing, ground rules |
| [THEMES.md](THEMES.md) | Making themes, colour keys, picture sizes and music |
| [HARNESS.md](HARNESS.md) | The isolated Dolphin setup in detail |
| [USB_HARDWARE_TEST.md](USB_HARDWARE_TEST.md) | Checking SD and USB image boot on a Wii with d2x |
| [CTGPR.md](CTGPR.md) | CTGP-R 1.03: how it is started, d2x's IOS reload block, what a console test must show |
| [RIIFS.md](RIIFS.md) | The RiiFS network-pack protocol and how RiftWii uses it |
| [HEADLESS.md](HEADLESS.md) | Starting a game through RiftWii from another loader |
| [BLUETOOTH.md](BLUETOOTH.md) | Research: other Bluetooth controllers without a cIOS, and what exists toward it |

## History

Written while RiftWii was being built, kept because code comments cite
their sections. They describe the project as it was on their dates;
where they disagree with the code, the code is right. "Conductor" and
"Muse" are the AI assistants that reviewed and wrote code under the
maintainer's direction (see "About AI assistance" in the main README).
"USB Loader GX Riiloaded" in them is an earlier, abandoned, unpublished
attempt to add Riivolution support to a fork of USB Loader GX; these
documents are the rules that kept its code out of RiftWii. Credit for
the parts of USB Loader GX that RiftWii does follow is in
[NOTICE.md](../NOTICE.md), checked against the code by
[ATTRIBUTION_AUDIT.md](ATTRIBUTION_AUDIT.md) (`tools/attribution_audit.py`).

| Page | What it was |
| --- | --- |
| [CONDUCTOR_REVIEW.md](CONDUCTOR_REVIEW.md) | The first review, 2026-09-18 |
| [CONDUCTOR_REVIEW_2.md](CONDUCTOR_REVIEW_2.md) | The runtime design and the gates it passed, with the Dolphin findings (section 23: why the runtime's code lives in MEM1) |
| [MUSE_HANDOFF_1.md](MUSE_HANDOFF_1.md) | Early implementation tasks |
| [HANDOFF_2026-09-20.md](HANDOFF_2026-09-20.md) | Savegame review fixes before the first hardware run |
