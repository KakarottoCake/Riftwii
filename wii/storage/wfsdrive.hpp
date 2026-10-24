// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

#include "riftwii/fat32.hpp"

namespace riftwii::wii {

// Dragon Quest X's WFS drive (riftwii/wfs.hpp, riftwii/wfstitle.hpp) as
// the USB scan found it. A drive whose first sector reads as random bytes
// (a Wii U's own drive looks the same) is tried with this Wii's WFS key
// (otpkey.hpp's ConsoleWfsKey): it opens only for the console whose
// IOS59 formatted it. What is installed on it is kept for Home and for
// the start of a WFS title; the drive is not read again after the scan.

// Tries the drive behind `reader`: true when it is this Wii's WFS drive.
// `sectors`: its capacity in 512-byte sectors, 0 when not known (it is
// worked out from the drive). `which` names the drive in the log.
bool ProbeWfsDrive(const BlockReader& reader, std::uint32_t sectors, const std::string& which);

// Before a scan: what the last one found is dropped.
void ForgetWfsDrive();

// Whether the last scan found this Wii's WFS drive.
bool WfsDriveFound();

enum class WfsInstall {
    Unchecked,  // no WFS drive was read (no key, or none plugged in): the game's own start says
    Missing,    // the drive was read, and the title is not on it or its install is not whole
    Ready,
};

// The install of a WFS title (00010004-S4MJ) on the drive the last scan
// read. `detail`, for the log, says what was found.
WfsInstall WfsInstallOf(std::uint64_t title_id, std::string& detail);

// The game the drive holds for a Dragon Quest X disc (by its ID6): the
// installed title's ID, or 0 when the drive was not read or has none.
std::uint64_t WfsTitleForDisc(const std::string& disc_id);

}  // namespace riftwii::wii
