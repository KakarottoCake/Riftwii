// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <set>
#include <string>
#include <vector>

// Cheat files: the plain-text Gecko code format the GeckoCodes archive
// serves and other loaders read (sd:/riftwii/cheats/<ID>.txt here):
//
//   SB4E01                      the game ID
//   Super Mario Galaxy 2        its name
//                               (blank line)
//   Infinite health [author]    a cheat's name
//   043CA24C 60000000           its code, one 8+8 hex pair per line
//   Any other line is a note.   (optional)
//                               (blank line before the next cheat)
//
// Codes that must be edited before use carry placeholders (X, Y, Z...)
// in place of hex digits; they are listed but cannot be turned on until
// the file is edited. The code handler reads the enabled codes as a GCT:
// 00D0C0DE 00D0C0DE, the codes, F0000000 00000000.
namespace riftwii {

struct Cheat {
    std::string name;
    std::vector<std::uint32_t> words;  // two per code line
    std::vector<std::string> notes;
    bool needs_values = false;         // placeholders left in: edit the file first
};

struct CheatFile {
    std::string game_id;
    std::string title;
    std::vector<Cheat> cheats;
};

// Lenient: lines it cannot place become notes. Fails only when the text
// holds no cheats at all.
bool parse_cheat_text(const std::string& text, CheatFile& out, std::string& error);

// The GCT of the cheats named in `enabled` (cheats that need values are
// skipped). `count` is how many went in.
std::vector<std::uint8_t> build_gct(const CheatFile& file, const std::set<std::string>& enabled, std::size_t& count);

}  // namespace riftwii
