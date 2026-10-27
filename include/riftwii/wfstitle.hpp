// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/disc.hpp"
#include "riftwii/wfs.hpp"

namespace riftwii {

// Dragon Quest X on the Wii: what its discs are, and what their installer
// leaves on a WFS drive (riftwii/wfs.hpp).
//
// The disc does not hold the game the way other discs do. Its game
// partition is a small launcher; a second partition of its own type
// (the "WFS installer", title 00010000-\0WIN) imports the game into a
// USB drive through IOS59's /dev/wfsi, from title WADs on the disc. The
// game is then a title of type WFS (00010004-S4MJ for every disc so far)
// split in two: its ticket, TMD, banner and a small boot program
// ("wfsboot") on the NAND, everything else on the drive, under
// /title/<group>/<code>/ (group: the maker code, "GD"):
//   content/default.dol   the game, which wfsboot loads through /dev/wfsi
//   content/...           its data
//   meta/<title ID>.tmd   the title's TMD, and one per add-on installed
//   save/, work/          what the game writes
// Started from a disc under a cIOS, the launcher finds no WFS (only
// Nintendo's IOS59 has it) and the disc can only offer to install.

// The partition type of the installer partition: its title ID's low half.
constexpr std::uint32_t kWfsInstallerPartition = 0x0057494E;  // "\0WIN"

// A disc that installs a WFS title rather than holding a game.
bool is_wfs_install_disc(const std::vector<PartitionEntry>& table);

// The TMD title type bit IOS59 gives WFS titles (Dolphin: "appears to be
// used for WFS titles"); wfsboot refuses titles without it.
constexpr std::uint32_t kTitleTypeWfs = 0x20;
bool tmd_is_wfs_title(const std::uint8_t* tmd, std::size_t size);

struct WfsTitle {
    std::string path;     // "/title/GD/S4MJ"
    std::string group;    // "GD"
    std::string code;     // "S4MJ"
    std::uint64_t title_id = 0;  // from its TMD; 0 when it has none
    std::uint16_t version = 0;
    std::uint32_t ios = 0;
    bool wfs_type = false;       // its TMD has the WFS title type
    bool program = false;        // content/default.dol is there
    bool program_checked = true; // false: looking for it failed (a read error), so `program` says nothing
    std::uint32_t program_bytes = 0;
    // The other TMDs in its meta folder: add-ons (later discs) installed
    // onto it, by title ID.
    std::vector<std::uint64_t> add_ons;
    std::vector<std::uint8_t> tmd;
};

// The titles installed on the drive. A folder that is not a title's, or
// a title whose TMD cannot be read, is left out and named in `notes`;
// false only when /title itself cannot be read. `complete`, when given,
// is true only when every listing and read the answer rests on worked:
// then a title not in `out` is not on the drive. A drive with no /title
// is complete (nothing is installed); a read error anywhere is not.
bool find_wfs_titles(const WfsVolume& volume, std::vector<WfsTitle>& out, std::vector<std::string>& notes,
                     std::string& error, bool* complete = nullptr);

// Which installed title a disc (by its ID6) installs: the one with the
// disc's code, else the one of its maker's group (every Dragon Quest X
// disc installs S4MJ, group GD). Only a title whose program is there.
// -1 when there is none.
int wfs_title_for_disc(const std::string& disc_id, const std::vector<WfsTitle>& titles);

// This Wii's WFS key as a key dump leaves it on the SD card: the 16 bytes
// themselves, or 32 hex digits (spaces, colons and line ends between them
// are passed over). False for anything else, or an all-zero key.
bool parse_wfs_key(const std::uint8_t* data, std::size_t size, std::uint8_t out[16]);

// "00010004-S4MJ" style text for logs.
std::string wfs_title_text(std::uint64_t title_id);

}  // namespace riftwii
