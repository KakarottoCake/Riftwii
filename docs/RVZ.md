# RVZ games

RVZ is Dolphin's compressed disc format. RiftWii reads it without turning
it back into a disc image: a Wii game reads its partition decrypted and
without hashes, and that is how RVZ stores it, so serving a read never
involves encryption, hashing or Nintendo's keys. Format: Dolphin's
`docs/WiaAndRvz.md`.

## Status

1. **Reader (done, host-tested).** `include/riftwii/rvz.hpp`:
   `read_rvz_head` and `check_rvz` decide from the uncompressed headers
   alone; `RvzImage` reads the disc outside partition data and each
   partition's data. `runtime/rtrvz.c` (freestanding, for the in-game
   runtime later) skips hash exception lists and decodes RVZ packing.
   Zstandard is decoded by `vendor-zstd/` (1.5.7, decoder only).
2. **Menu and loader (done; Dolphin-tested).** `.rvz` files in
   `sd:/games` and `usb:/games` are listed with the ISOs. Picking one
   reads only its headers (`wii/storage/usbcatalog.cpp`): a refused file says why,
   one at the player's own risk shows its warning on the first press of
   Start and plays on the second. At launch the loader writes the stub
   (`sd:/riftwii/rvz/<ID>.stub`, the disc's first 0x50000 bytes and every
   partition header, about 0.6 MB) and gives d2x a fragment list that
   places it on an otherwise empty disc. The stub is on the SD card even
   for an RVZ on the USB drive: d2x's disc is then the card, and the
   loader never writes to the drive. The RVZ itself is read from the
   drive through d2x's USB device, `/dev/usb2` (`wii/storage/umsdev.cpp`), once
   the loader is in the cIOS, since libogc's USB driver and d2x's cannot
   both own the drive (`wii/storage/umsdev.cpp`, which packs on the USB drive use
   too: one handle serves both). `di::read` answers partition
   reads from the RVZ (`di::set_partition_resolver`), so the probe, the
   FST for the packs and the apploader's loads all come from the image,
   and the loader keeps the running IOS so the card stays up through the
   apploader.
   Checked in Dolphin, which has no d2x: `rvzstub` (host tool) writes the
   stub as a disc image, Dolphin boots it, and autorun's `rvz <sd path>`
   serves the partitions from the RVZ. Deca Sports 2 is probed, laid out
   and loaded by its apploader from the RVZ (2.2 MB of DOL).
3. **In-game reads (done; Dolphin-tested, not yet on a Wii).** RVZ games
   get their own runtime blob, `riftwii_rt_rvz.bin`: the usual one built
   with `RT_RVZ`, RVZ packing (`runtime/rtrvz.c`) and Zstandard
   (`runtime/resident/rt_zstd.c`, 46 KB at `-Os`). See below.
   Deca Sports 2 (128 KiB chunks, Zstandard level 10) reaches its title
   screen and plays its attract demo in Dolphin, every read served from
   the RVZ: 735 reads, 365 groups decoded, none failed. Kirby's Epic Yarn
   boots from its RVZ with a test pack on top (a file changed in place,
   one relocated, one created).

Both runtime blobs are embedded in the loader zstd-compressed
(`Makefile.runtime`, `zstd -19`: 111 KB to 53 KB for this one, 57 KB to
27 KB for the usual one), and the one a game needs is unpacked when it
starts, with the Zstandard the loader has for RVZ headers anyway. The
loader's own Zstandard is built with `-Os`. RVZ support costs the menu
about 160 KB of MEM1 in all.

## How the game's reads are served

At launch the loader writes the game partition's group table to the card
(`sd:/riftwii/rvz/<ID>.groups`, 8 bytes per group: where it is in the
file and how it is stored) and gives the runtime the sectors of that file
and of the RVZ (`RvzImage::runtime_table`, `rvz_resident_options`). The
runtime's data block in MEM2 holds its state, one buffer for a group as
stored, one for it decoded, and the decoder's workspace. The buffers are
sized from the RVZ's own table (`rvz_buffer_sizes`): the first holds its
largest compressed group, the second its largest group decoded or
stored as it is. An RVZ that compresses nothing needs neither the first
buffer nor the decoder. Kirby's Epic Yarn (128 KiB chunks): 126 KB and
136 KB, and 48 KB for the decoder.

The hook answers every DVDLowRead itself; the drive never sees a
partition read, since the disc d2x presents holds only the stub. It
issues a request that does nothing (d2x's `ISINSERTED`, or `GETSTATUS` on
`/dev/sdio/slot0`) with the runtime's completion entry as its callback,
so the read is served from the IPC interrupt like a disc reply. The
completion then walks the read group by group: one sector of the group
table when the group's entry is not in the one held, the group's stored
bytes in requests of at most 32 KiB that never cross a piece of the file,
then Zstandard, the hash exception lists skipped, and the bytes copied or
unpacked straight into the game's buffer. The last group decoded is kept,
so the small reads games make in a row cost one decode. On
`/dev/sdio/slot0` with a savegame folder, each read of the card first
waits (null round trips) while a savegame command has the card, and
savegame commands wait for it: the card takes one command at a time. A mod's table
still applies on top (memory, files on the card or the USB drive,
relocated files: `tests/rvz_tests.cpp` checks every edge of each against
the RVZ's bytes), and what it leaves to the disc comes from the RVZ. A read past the partition's data fails as it
does on a disc (layer checks rely on that), and so does one that arrives
while another is being served.

Zstandard's constant tables mean the RVZ blob is not position
independent like the other one. The build links it at two addresses,
`tools/rtreloc.py` writes its absolute references (`riftwii_rt_rvz.rel`,
233 of them) and checks that applying them gives the second link, and the
loader applies them for the address it installs the blob at
(`apply_resident_relocs`).

An RVZ on the USB drive is read the same way. Its groups come from the
drive through d2x's `/dev/usb2` (`IOCTL_UMS_READ_SECTORS`, the same
request as d2x's SD read), on the handle the loader opened (the
context's `usb_fd`, which packs on the drive use too): d2x refuses to
open the device once a title runs. The group table and the request
that starts each read stay on the card. The host tests cover this with
the RVZ on a separate fake drive; Dolphin has no d2x, so only a Wii can
check it for real.

Still to do: a real Wii (RVZ on USB, and decode time per group, which
decides the chunk limits below), and games whose RVZ is in more than 1024
pieces on the drive (refused with a message to copy it again).

## What plays

`check_rvz` puts every file in one of three groups. The menu shows the
reasons; the boot is refused before anything is loaded when a file is
unsupported.

| | Supported | At your own risk | Refused |
| --- | --- | --- | --- |
| Format | RVZ 0.03 to 1.00 | RVZ newer than 1.00 that says 1.00 readers can read it | WIA; RVZ that needs a newer reader; RVZ before 0.03 |
| Compression | Zstandard (any level up to 22), none | Zstandard level above 22 | bzip2, LZMA, LZMA2 (too slow on a Wii); unknown methods |
| Chunk size | 32 to 128 KiB | 256 and 512 KiB | 1 MiB and more (too large to decode during play) |
| Disc | Wii, GameCube | | a Wii disc without partitions; a GameCube disc that claims some |
| File | complete | | shorter or longer than its header says |

The chunk-size limits are provisional until a real Wii is measured. Zstandard's level is not a real limit: decoding costs the same at
every level (Super Mario Galaxy 2 at levels 5 and 22 decodes at the same
speed and the file differs by 0.7%), and most RVZs in the wild use 22.

## GameCube games

A GameCube RVZ is the simpler case: no partitions, no encryption, no
hashes. The whole disc is raw data in groups of one chunk each, stored,
compressed, or RVZ-packed; GameCube padding comes from the same generator
as a Wii disc's, seeded by the disc's ID and its offset on the disc, and
RVZ packing keeps the seed, so the reader only needs the disc offset of
the group's start.

`sd:/games/<folder>/game.rvz` is listed as a GameCube game, as an ISO is
(`ScanGcImages`), with its ID, title, cover and the disc's own banner,
read through `RvzDiscSource` (`OpenGcDisc`). `check_rvz` gives it the same
verdicts as a Wii game's: the table above. A refused one says why at
Start; otherwise the first press of Start shows the note every RVZ gets
(experimental), with the reasons when it plays at the player's risk, as
for a Wii game. A GameCube RVZ elsewhere (`sd:/wbfs`, the top of `sd:/games`, the USB drive)
is listed refused, with a pointer to the folder it goes in. Patch-only
mods (`sd:/riftwii/patches`) are made from ISOs only.

At launch (`plugins/gamecube/gcnative.cpp`) the loader reads boot.bin, bi2.bin,
the FST and the DOL from the RVZ, writes the group table for the disc to
`sd:/riftwii/rvz/<ID>.groups` (8 bytes a group, as for Wii games) and
starts the game with the blob's RVZ build, `riftwii_gc_rvz.bin`: the usual
blob built with `RT_RVZ`, plus `runtime/gc/gc_rvz.c`, RVZ packing and
Zstandard. Like the Wii RVZ runtime it is relocated where the loader puts
it (`riftwii_gc_rvz.rel`), and it takes 50 KB more of MEM1's top than the
ISO blob (74 KB): a game that keeps its stack there needs its ISO, and the
launch says so. The plain blob stays position independent.

In game, a read of the image (`GC_RUN_RVZ` runs; a pack's own files still
come from the card) finds its group, reads the table sector that holds its
entry, fetches the group's stored bytes in requests of at most 64 KiB,
decodes them, and copies or unpacks them into the game's buffer. The
alarm's 64 KB pieces of a read (`gc_read_step`) end where a group does, so
one turn of the alarm decodes at most one group, and the decoding counts
in the read's time: with Loading speed set to As on a GameCube the callback waits
for the drive's time only when that is longer, and with Fast it comes as
soon as the bytes are there. Two groups are kept decoded, one for the
game's reads and one for disc audio streaming (`gc_stream_fetch`), so the
music read ahead does not throw out the group the game reads through.
The buffers are in MEM2 at `0x91300000` (2 MB set aside, sized from the
RVZ's own table: about 430 KB with 128 KiB chunks, about 1.6 MB with
512 KiB ones); a table that needs more is refused at launch. The trace
records the groups decoded, the time spent and the last error.

Checked: Luigi's Mansion PAL (Rev 1) from Dolphin's own RVZ (Zstandard
level 19, 128 KiB chunks, 8981 of its 11139 groups RVZ-packed) reads byte
for byte as the ISO through both `RvzDiscSource` and the blob's reader on
the host, and plays its title screen in Dolphin; Super Mario Eclipse from
an RVZ made without packing does too, and Home's Channels view shows both
games' disc banners read from their RVZs. Not yet checked on a Wii (decode
time per group on Broadway decides the chunk limits, as for Wii games).

## Tests

`tests/gcrvz_tests.cpp` writes a synthetic GameCube disc as an RVZ itself
(groups of zeros, stored, Zstandard frames, and packed padding from odd
offsets across 0x8000 boundaries; Zstandard and no compression; 32 to
128 KiB chunks) and reads it back byte for byte through `RvzDiscSource`
and `runtime/gc/gc_rvz.c`, the file in pieces on a fake card, with the
failures (past the end, a failed request, a sector outside the pieces, a
buffer too small).

`tests/rvz_tests.cpp` reads the images in `tests/fixtures/rvz/`, made by
`tools/rvz/make_test_disc.py` from a disc built from scratch (encrypted
and hashed like a real one, with Wii-style padding and one wrong hash so
exception lists are exercised) and converted by DolphinTool in seven
variants. `manifest.txt` holds the SHA-1 of every 0x8000 bytes a reader
must return. Regenerating gives the same files. The same tests drive the
runtime (built for the host, with the host's Zstandard): every image,
through d2x's and `/dev/sdio/slot0`'s requests, with the file in pieces
on a fake card, must return the reader's bytes, and reads past the end,
reads while busy and a failed card request must fail cleanly.

Checked by hand against real games, outside the test suite: every
partition sector of Super Mario Galaxy 2, from its RVZ, matches the ISO
DolphinTool makes of it, decrypted independently. Super Mario Galaxy and
Newer Super Mario Bros. Wii decode without errors.
