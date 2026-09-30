// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "boot.hpp"
#include "riftwii/launch.hpp"
#include "riftwii/patch.hpp"
#include "rtable.h"

// Compiles a Riivolution-format package on the SD card into what the boot
// needs: the redirect table's entries (external bytes as SD sectors,
// untouched bytes of relocated files as DISC ranges, padding as ZERO) and
// the FST relocations of files whose size changed or which are created.
// Everything the host tests cover does the work (riftwii/patch.hpp,
// expand.hpp, apply.hpp, redirect.hpp); this file only supplies the disc
// and SD sides of ContentProvider (reads and directory listings) and the
// SD placer. Runs while the card is mounted and the partition is open.
namespace riftwii::wii {

struct CompiledMod {
    // Set by CompileSelection. BootCompiled rejects a later probe that is not
    // this exact ID6/revision/disc-number identity.
    DiscIdentity source_identity;
    bool has_source_identity = false;
    std::vector<std::string> xml_paths;
    std::vector<rt_entry> entries;
    // Bytes the table serves from memory: small files on an NTFS USB drive,
    // which have no sectors of their own.
    std::vector<MemReplacement> mem;
    std::vector<FstRelocation> relocations;
    std::vector<MemoryPatch> memory;  // values read (valuefile resolved), in package and document order
    std::string savegame_dir;         // sd:/ folder of the one <savegame external> selected, empty = none
    bool savegame_clone = false;      // any selected <savegame> asked for clone (the default)
    std::vector<std::string> notes;  // one line per folder, patched file and memory patch, for the log
    std::vector<std::string> warnings;  // from the package parser
    // The game's executable, when a patch names the bare file "main.dol"
    // (as Riivolution and Dolphin treat it; CT-CODE packs replace it this
    // way): the whole patched DOL, for the apploader only.
    std::vector<std::uint8_t> main_dol;
};

// A package and the choices to apply over its defaults, in order (see
// riftwii::select_choice for the name forms): the frontend's own type.
using PackageSelection = PackageChoices;

// Compiles the packages together, in the order given: patches from later
// packages that touch a file an earlier one patched apply on top of its
// result, as later patches inside one package do.
bool compile_packages(const std::vector<PackageSelection>& packages, const DiscProbe& probe,
                      const OpenedPartition& partition, CompiledMod& out, std::string& error);

// Whether the chosen packs only replace the game's executable with a
// Homebrew Channel app on the SD card (a <file disc="main.dol"> whose
// external is /apps/<folder>/<name>.dol), as CTGP Revolution 1.03's pack
// does with its channel. Such an app is a launcher of its own, made to be
// started by the Homebrew Channel (its IOS and hardware access kept), not
// loaded as the game. Read from the packs alone, before the disc or an
// IOS reload. `app` gets its sd: path; a save redirect in the same choice
// cannot apply to it and is named in `dropped`. Other files, folders or
// memory patches, packs on the USB drive or from a PC: no.
bool homebrew_app_stand_in(const std::vector<PackageSelection>& packages, const std::string& game_id,
                           std::string& app, std::string& dropped);

}  // namespace riftwii::wii
