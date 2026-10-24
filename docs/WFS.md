# Dragon Quest X and its WFS USB drive

Dragon Quest X (Wii, Japan) does not run from its disc. The disc installs
the game onto a USB drive formatted with WFS, the Wii U's file system
backported to the Wii, and only IOS59 has the drivers that format and read
that drive (`/dev/usb/wfssrv`, `/dev/wfsi`). This page says how RiftWii
finds and starts it.

## The disc

| Partition | What |
| --- | --- |
| DATA | A small launcher (00010004-S6TJ, IOS59) |
| `\0WIN` (type `0x0057494E`) | The WFS installer: the game as WADs (`S4MJ260.wad`, `S4MJ261.wad`) |
| UPDATE | IOS59 |

The Wii Menu installs 00010004-S4MJ: a disc channel (title type `0x21`,
the `0x20` bit marks a WFS title) whose NAND keeps only the banner
(content 0) and the boot program `wfsboot` (the last content). Its other
contents (type `0x4001`) are unpacked by `/dev/wfsi` onto the drive as
`/title/GD/S4MJ/{content/default.dol, meta/<title>.tmd, save, work}`.

## What RiftWii does

- **The drive.** The USB scan (`wii/storage/usbcatalog.cpp`) sees a WFS
  drive as random bytes, as it sees a Wii U's own drive. It then tries
  the drive with this Wii's WFS key (`ConsoleWfsKey`,
  `wii/menu/wfskey.cpp`): a drive that opens is this Wii's WFS drive
  (`wii/storage/wfsdrive.cpp`), and Home's status says so instead of
  "Wii U drive". What is installed on it (`find_wfs_titles`,
  `src/fs/wfstitle.cpp`) is kept for the start. The reader
  (`src/fs/wfs.cpp`) is read-only; RiftWii never writes the drive.
- **The listing.** 00010004-S4MJ on the Wii's NAND is listed with the
  Wii's channels (`ChannelKind::Wfs`, `wii/storage/nandchannels.cpp`).
  Titles in an emulated NAND on the card are never WFS titles: IOS59 on
  the real NAND is what reads the drive.
- **The start.** `wfsboot` asks IOS59 for the game on the drive, and
  IOS59 refuses titles that ES_Identify set up, so RiftWii's loader
  cannot start it: the system starts it with `WII_LaunchTitle`, as the
  Wii Menu does (`StartWfsTitle`, `wii/main.cpp`). When the scan read
  the drive and the game is not on it (or its install is not whole),
  RiftWii goes back to its menu and says why instead.
- **Disc images.** An image of the disc (WBFS, ISO, RVZ) would only run
  the installer, which needs IOS59 and a real disc; RiftWii stops with a
  message that says so and names the installed game when the drive has
  it (`Session::ensure_probe`, `wii/launch/launchrun.cpp`). A real disc
  in the drive starts as any disc does.

## The key

The drive is encrypted with a key IOS59 keeps for each console. RiftWii
does not work it out: `ConsoleWfsKey` reads the one a key dump left on
the card, `sd:/riftwii/wfskey.bin`, as the 16 bytes or as 32 hex digits
(`parse_wfs_key`, `src/fs/wfstitle.cpp`). Like the other console keys it
is never logged or kept. Without the file the drive is not read, Home
still calls it a Wii U drive, and the game still starts from Wii
channels (the system finds out whether it is installed).

## Checking it in Dolphin

Dolphin emulates IOS59's `/dev/wfsi` and `/dev/usb/wfssrv` with a host
folder (`<user>/WFS/vol/msc01`, the drive already decrypted), so the
listing and the start can be checked there; the USB scan and the key
cannot (Dolphin has no WFS USB drive to read, so the install check says
"no WFS drive was read" and the start goes ahead). To set it up, as the
installer would: the title's TMD and ticket, the banner and `wfsboot`
(its type 1 contents) go in the NAND under their content IDs, and each
type `0x4001` content, a U8 archive, is unpacked into
`WFS/vol/msc01/title/GD/S4MJ/content`, with the TMD at
`.../meta/0001000453344d4a.tmd`. Then, with Home showing all games
(`view=all` in `sd:/riftwii/settings.txt`), Dragon Quest X is listed from
the Wii with its cover, and Play logs the WFS start; Dolphin's log
(`IOS_ES`, `IOS_WFS`) shows ES starting 00010004-S4MJ on IOS59, `wfsboot`
running, and `IOCTL_WFSI_LOAD_DOL` loading `default.dol` from the drive,
as when Dolphin starts the title itself. The game then reads its files
from the drive; the disc and its patch do not hold its menus (the game
downloads them), so it goes no further in Dolphin.

## The format

Read from wfslib's layout (NOTICE.md has the commits): 4 KB physical
blocks, AES-128-CBC per block with an IV built from the block's address,
the area's IV and the drive's size; metadata blocks hashed with SHA-1;
areas and quotas (4 KB and 8 KB blocks); directories as radix trees;
files inline, in blocks, large blocks or clusters. When the drive's size
is not known it is found from block 0: a wrong IV garbles only its first
16 bytes, so the header gives the formatted size and block 0's hash
proves it. `tests/wfs_tests.cpp` writes drives of every layout with
`tests/wfs_image.hpp` and reads them back; the reader was also checked
against wfslib on drives wfslib wrote (707 files, none differing).
