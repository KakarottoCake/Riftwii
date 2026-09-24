# RiftWii documentation

For players, the project's `README.md` covers installing and using
RiftWii. These pages are for people working on it.

## Current

| Page | What it covers |
| --- | --- |
| [ARCHITECTURE.md](ARCHITECTURE.md) | How a launch works, which file owns what, the memory map, the files on the SD card |
| [DEVELOPING.md](DEVELOPING.md) | Building, tests, Dolphin runs, test scripts, releasing, ground rules |
| [HARNESS.md](HARNESS.md) | The isolated Dolphin setup in detail |
| [USB_HARDWARE_TEST.md](USB_HARDWARE_TEST.md) | Checking SD and USB image boot on a Wii with d2x |
| [RIIFS.md](RIIFS.md) | The RiiFS network-pack protocol and how RiftWii uses it |

## History

Written while RiftWii was being built, kept because code comments cite
their sections. They describe the project as it was on their dates;
where they disagree with the code, the code is right.

| Page | What it was |
| --- | --- |
| [CONDUCTOR_REVIEW.md](CONDUCTOR_REVIEW.md) | The first review, 2026-09-18 |
| [CONDUCTOR_REVIEW_2.md](CONDUCTOR_REVIEW_2.md) | The runtime design and the gates it passed, with the Dolphin findings (section 23: why the runtime's code lives in MEM1) |
| [MUSE_HANDOFF_1.md](MUSE_HANDOFF_1.md) | Early implementation tasks |
| [HANDOFF_2026-09-20.md](HANDOFF_2026-09-20.md) | Savegame review fixes before the first hardware run |
