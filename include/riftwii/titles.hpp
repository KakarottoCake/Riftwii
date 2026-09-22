// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <set>
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
    std::size_t size() const { return titles_.size(); }

private:
    std::map<std::string, std::string> titles_;
};

// "Super Mario Galaxy 2" from ".../wbfs/Super Mario Galaxy 2 [SB4E01]/SB4E01.wbfs";
// empty when the image is not in a "Title [ID]" folder.
std::string folder_title(const std::string& path, const std::string& id);

// The name to show: database, then folder, then the internal name.
std::string display_title(const TitleTable* table, const std::string& id, const std::string& path,
                          const std::string& internal);

}  // namespace riftwii
