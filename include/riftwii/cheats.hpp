// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
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
//
// A file may hold codes for several releases of the game, each under a
// heading line of its own (Super Smash Bros. Melee's has "Version 1.0" and
// "Version 1.1"; the addresses differ between them):
//
//   Version 1.0                 a heading: a line alone between blank lines
//                               (blank line)
//   Some cheat [author]         this release's codes, up to the next heading
//   ...
//
// A heading names the disc's revision, the byte at offset 7 of its header
// (boot.bin): "Version 1.0", "Version 1.00", "v1.0", "NTSC 1.00" are
// revision 0; "1.1" and "1.01" revision 1; "1.02" revision 2; "Rev 1" and
// "Revision 1" revision 1. Given the disc's revision, the functions below
// see only the cheats under its heading and the ones before any heading;
// a file without headings is read whole, as before.
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

// Every revision: no heading is skipped.
constexpr int kAnyRevision = -1;

struct CheatFile {
    std::string game_id;
    std::string title;
    std::vector<Cheat> cheats;
    // The revisions the file has headings for, in the order they come,
    // whether or not their cheats were read.
    std::vector<std::uint8_t> revisions;
    // Cheats left out because they are for another revision.
    std::size_t other_revision_cheats = 0;
};

// The revision a heading line names (see above), or -1 when the line is
// not a heading.
int cheat_revision_heading(const std::string& line);

// Lenient: lines it cannot place become notes. Fails only when the text
// holds no cheats at all (for `revision`, when not kAnyRevision: the disc
// header's revision byte).
bool parse_cheat_text(const std::string& text, CheatFile& out, std::string& error, int revision = kAnyRevision);

// The GCT of the cheats named in `enabled` (cheats that need values are
// skipped). `count` is how many went in.
std::vector<std::uint8_t> build_gct(const CheatFile& file, const std::set<std::string>& enabled, std::size_t& count);

// A fresh download (`fresh`) put together with the file it replaces
// (`old`), so a download never loses the player's own work: cheats in
// `old` that the download does not have (added by hand) go at the end,
// and a cheat whose placeholders were filled in by hand in `old` stays
// filled in. A cheat the player has turned on (`enabled`, names as
// parse_cheat_text gives them) keeps its own code when the download's
// differs: an enabled cheat runs at the next start, and a download must not
// change what it does behind the player's back (SEC-02). Everything else is
// the download's. `kept` is how many of `old`'s cheats were kept, `held`
// how many of those were enabled ones whose code the download changed.
// Text in, text out ("\n" lines). Cheats are matched by name within the
// same revision heading.
std::string merge_cheat_text(const std::string& fresh, const std::string& old, std::size_t& kept,
                             const std::set<std::string>& enabled = {}, std::size_t* held = nullptr);

// The placeholders of cheat `name` (as parse_cheat_text names it) in
// `text`, in the order they first come. Empty when it has none (or no
// such cheat).
std::vector<CheatField> cheat_fields(const std::string& text, const std::string& name,
                                     int revision = kAnyRevision);

// Puts `fields`' values (hex, at most each one's digits; padded with
// zeros in front) in place of cheat `name`'s placeholders, keeping its
// codes as they were in template lines. False, with `error`, when a value
// is missing or not hex, or there is no such cheat.
bool fill_cheat_values(std::string& text, const std::string& name, const std::vector<CheatField>& fields,
                       std::string& error, int revision = kAnyRevision);

// Whether `gct` is a code list: the 00D0C0DE header, whole codes (8 bytes
// each) and the F0000000 00000000 end.
bool valid_gct(const std::vector<std::uint8_t>& gct);

// One list running `first`'s codes, then `second`'s (both valid; either
// may be empty, meaning none).
std::vector<std::uint8_t> join_gct(const std::vector<std::uint8_t>& first, const std::vector<std::uint8_t>& second);

}  // namespace riftwii
