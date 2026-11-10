// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// The settings CTGP-R's in-game code reads from the loader's data block at
// 0x816ff4e0..0x816ffb00 (docs/CTGPR.md). 24 of those words are
// sd:/ctgpr/config.ini options: the channel keeps a table of (key, variable
// address, kind, range) and parses the file into it. This is that table with
// the defaults the channel sets before it parses the file.
namespace riftwii {

struct CtgpField {
    const char* key;
    std::uint32_t address;  // where the in-game code reads the word
    bool is_bool;           // "yes"/"no", stored as 1/0
    std::int32_t min;       // integers only
    std::int32_t max;
    std::int32_t def;
};

// The table, as a constant array (no constructor runs for it: the ctgpr
// plugin has no static constructors, docs/PLUGINS.md).
struct CtgpFieldList {
    const CtgpField* first;
    std::size_t count;
    const CtgpField* begin() const { return first; }
    const CtgpField* end() const { return first + count; }
    std::size_t size() const { return count; }
    const CtgpField& operator[](std::size_t i) const { return first[i]; }
};
CtgpFieldList ctgp_fields();

// config.ini as key -> value (sections ignored, "#" and ";" lines skipped,
// the last of a repeated key wins).
std::map<std::string, std::string> ctgp_parse_ini(const std::string& text);

// An ini yes/no value, in any letter case ("Yes", "no", "TRUE"; also 1 and 0).
// False, with `out` untouched, for anything else. The channel's table and the
// launch's own settings read booleans through this one rule.
bool ctgp_parse_ini_bool(const std::string& value, bool& out);

struct CtgpWordWrite {
    std::uint32_t address;
    std::uint32_t value;
};

// The words to place in the game's memory: every field at its default, then
// the options config.ini sets. Unknown keys and values that do not parse or
// lie outside the option's range are ignored (the default stays). Sections
// are ignored, as the channel's own table has no section names. `lanmode`
// is never taken from the file: LAN play stays off.
std::vector<CtgpWordWrite> ctgp_field_writes(const std::string& config_ini);

}  // namespace riftwii
