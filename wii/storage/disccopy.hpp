// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "riftwii/discdump.hpp"

// Settings > Copy a game disc: the Wii disc in the drive copied to a
// .wbfs file in wbfs/ on the SD card or a FAT32 USB drive
// (riftwii/discdump.hpp), where Home then lists it. Every partition of the
// disc goes into the file, the update and any channel installer as well
// as the game. Reading the encrypted partitions needs a d2x cIOS: under
// the Wii's own IOS the menu restarts under one for the session first.
namespace riftwii::wii {

// A drive a copy can go to: mounted for writing (libfat), so FAT32 only.
struct DiscCopyPlace {
    std::string root;  // "usb:/" or "sd:/"
    bool usb = false;
    std::uint64_t free_bytes = 0;
};
std::vector<DiscCopyPlace> DiscCopyPlaces();
// A USB drive is read, but none of its partitions is FAT32 (NTFS, say), so
// it is not among DiscCopyPlaces.
bool DiscCopyUsbNotFat32();

// Whether the menu can read a whole disc now: it runs under a d2x cIOS, or
// one whose d2x information could not be read (PrepareDiscCopy then reads
// past the start of the disc before anything else, and says when it can't).
// Never in Dolphin, which refuses these reads as the Wii's own IOS does.
bool DiscCopyCanRead();
// The cIOS the menu restarts under to copy a disc: a d2x on base 58 first,
// since only that one also reads USB drives in the menu, else any other
// d2x, last a cIOS whose d2x information could not be read. 0 when there
// is none.
int DiscCopySlot();

// The disc in the drive, looked into.
struct DiscCopyJob {
    std::string id;
    std::string title;
    DiscUsage usage;
    WbfsImagePlan plan;
    unsigned whole_partitions = 0;  // kept whole, their files not read (the log says why)
};
// Reads the disc's header and works out what the copy keeps. False with
// `error` (said to the player) when there is no Wii disc or it cannot be
// read.
bool PrepareDiscCopy(DiscCopyJob& job, std::string& error);

// The file the copy makes on `place`: <root>wbfs/<title> [<ID>]/<ID>.wbfs.
std::string DiscCopyPath(const DiscCopyJob& job, const DiscCopyPlace& place);

// Copies the disc to DiscCopyPath, in pieces of kWbfsSplitBytes. `progress`
// is told the bytes copied and in all; false from it stops the copy. What
// was written is removed again when the copy does not finish. An image
// already there is refused (`exists` set) rather than overwritten.
bool RunDiscCopy(const DiscCopyJob& job, const DiscCopyPlace& place, const DiscCopyProgress& progress, bool& exists,
                 std::string& error);

}  // namespace riftwii::wii
