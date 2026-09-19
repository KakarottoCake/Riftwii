// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "boot.hpp"
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
    std::string xml_path;
    std::vector<rt_entry> entries;
    std::vector<FstRelocation> relocations;
    std::vector<MemoryPatch> memory;  // values read (valuefile resolved), in document order
    std::vector<std::string> notes;  // one line per patched file, for the log
    std::vector<std::string> warnings;  // from the package parser
};

// `window_cursor` is the next free byte of the virtual window (in/out), so
// several packages can be compiled one after the other.
bool compile_package(const std::string& xml_sd_path, const DiscProbe& probe, const OpenedPartition& partition,
                     std::uint64_t& window_cursor, CompiledMod& out, std::string& error);

}  // namespace riftwii::wii
