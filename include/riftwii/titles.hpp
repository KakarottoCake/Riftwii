// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <set>
#include <cstddef>
#include <string>

namespace riftwii {

// Display names for games. A disc header carries the developer's internal
// name ("SUPER MARIO GALAXY MORE" for Super Mario Galaxy 2), so the game
// list prefers, in order: a title database line for the game's ID, the
// name of the folder the image sits in (backup managers name folders
// "Title [ID]"), and only then the internal name.

// "ID = Title" lines, as in GameTDB's titles.txt (its header line and any
// line without " = " are skipped). IDs are 4 or 6 characters.
class TitleTable {
public:
    // Adds the lines of `text`. With `wanted`, keeps only those IDs (6-char
    // IDs and their 4-char prefixes both count), so a database of thousands
    // of games costs only the handful on the drive.
    void add_text(const std::string& text, const std::set<std::string>* wanted = nullptr);
    // The title for a 6-char game ID, or its 4-char prefix; null when none.
    const std::string* find(const std::string& id) const;
    // The title for exactly this ID, without the 4-char prefix's; null when none.
    const std::string* find_exact(const std::string& id) const;
    // The title for a 4-char game code (a save's or a channel's): the code's
    // own line, else the first 6-char ID it starts (RSBE for RSBE01); null
    // when none.
    const std::string* find_code(const std::string& code) const;
    std::size_t size() const { return titles_.size(); }

private:
    std::map<std::string, std::string> titles_;
};

// Where a game's name is sorted from: past a leading "The " (any case), so
// "The Legend of Zelda: Twilight Princess" goes under L, as game lists do.
// 0 when the name has no such word (or is only it).
std::size_t sort_name_start(const std::string& title);

// A name as lists sort it: ASCII letters in lower case and the accented
// Latin letters (U+00C0 to U+017F) as their plain ones, so "apps" sorts
// beside "Apps" and "Ōkami" among the O's ("Æ" and "Œ" as two
// letters, "ß" as "ss"). Anything else is kept as it is and sorts after
// the letters, byte by byte.
std::string sort_key(const std::string& name);

// Whether `a` sorts before `b`: by sort_key, then (names that only differ
// in case or accents) by their bytes, so the order never depends on how a
// list came in.
bool sort_name_less(const std::string& a, const std::string& b);

// The letter a game's name is filed under, as Home sorts it: past a leading
// "The", its first letter in upper case with accents taken off ('O' for
// "Ōkami"); '#' for a digit or a sign, which sort before A; '~' for any
// other script (Japanese, Korean...), which sorts after Z, so each group is
// at one end of the list only.
constexpr char kOtherScriptLetter = '~';
char sort_letter(const std::string& title);

// "Super Mario Galaxy 2" from ".../wbfs/Super Mario Galaxy 2 [SB4E01]/SB4E01.wbfs";
// empty when the image is not in a "Title [ID]" folder.
std::string folder_title(const std::string& path, const std::string& id);

// The 6-character game ID a backup manager put in an image's name, so a
// big drive can be listed without opening every image: the file's stem
// (".../RUUE01.wbfs"), else a "[ID]" at the end of the folder's or the
// file's name. Empty when the name carries none.
std::string id_from_image_path(const std::string& path);

// The name to show: database, then folder, then the internal name. A
// folder named for another ID than the disc's (a mod kept under its own
// ID) comes before the database, whose name is the original game's.
std::string display_title(const TitleTable* table, const std::string& id, const std::string& path,
                          const std::string& internal);

}  // namespace riftwii
