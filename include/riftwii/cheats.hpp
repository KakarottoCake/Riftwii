// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
// their values are filled in (on a computer, or in RiftWii, which keeps
// the codes as they were in "Template: 28XXXXXX YYYY0000" lines, so the
// values can be changed again). The code handler reads the enabled codes as a GCT:
// 00D0C0DE 00D0C0DE, the codes, F0000000 00000000.
namespace riftwii {

struct Cheat {
    std::string name;
    std::vector<std::uint32_t> words;  // two per code line
    std::vector<std::string> notes;
    bool needs_values = false;         // placeholders left in: edit the file first
    bool has_template = false;         // values filled in by RiftWii: they can be changed
};

// A placeholder of a cheat's codes: its letter, how many hex digits it
// takes, and its value now (empty while it is still a placeholder).
struct CheatField {
    char letter = 0;
    std::size_t digits = 0;
    std::string value;
};

constexpr const char* kCheatTemplateNote = "Template: ";

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

// A fresh download (`fresh`) put together with the file it replaces
// (`old`), so a download never loses the player's own work: cheats in
// `old` that the download does not have (added by hand) go at the end,
// and a cheat whose placeholders were filled in by hand in `old` stays
// filled in. Everything else is the download's. `kept` is how many of
// `old`'s cheats were kept. Text in, text out ("\n" lines).
std::string merge_cheat_text(const std::string& fresh, const std::string& old, std::size_t& kept);

// The placeholders of cheat `name` (as parse_cheat_text names it) in
// `text`, in the order they first come. Empty when it has none (or no
// such cheat).
std::vector<CheatField> cheat_fields(const std::string& text, const std::string& name);

// Puts `fields`' values (hex, at most each one's digits; padded with
// zeros in front) in place of cheat `name`'s placeholders, keeping its
// codes as they were in template lines. False, with `error`, when a value
// is missing or not hex, or there is no such cheat.
bool fill_cheat_values(std::string& text, const std::string& name, const std::vector<CheatField>& fields,
                       std::string& error);

// Whether `gct` is a code list: the 00D0C0DE header, whole codes (8 bytes
// each) and the F0000000 00000000 end.
bool valid_gct(const std::vector<std::uint8_t>& gct);

// One list running `first`'s codes, then `second`'s (both valid; either
// may be empty, meaning none).
std::vector<std::uint8_t> join_gct(const std::vector<std::uint8_t>& first, const std::vector<std::uint8_t>& second);

}  // namespace riftwii
