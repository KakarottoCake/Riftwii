// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/casecolour.hpp"

#include <cctype>
#include <cstring>
#include <string_view>

namespace riftwii {
namespace {

// Twelve characters per game, sorted: the ID, then rrggbb.
const char kColours[] =
#include "casecolours.inc"
    ;
constexpr std::size_t kCount = (sizeof(kColours) - 1) / 12;

int Hex(char c) { return c <= '9' ? c - '0' : c - 'a' + 10; }

// Series whose cases share one colour in every region and edition, so a
// copy GameTDB does not list (another region, a mod keeping the game's
// first letters) still gets it. By the ID's first three characters.
struct Series {
    const char* prefix;
    std::uint8_t rgb[3];
};
const Series kSeries[] = {
    {"SMN", {0xff, 0x00, 0x00}},  // New Super Mario Bros. Wii: red
};

// The ID, upper case, if it is three to six letters and digits. The
// length is checked first, so a long run of junk is never copied.
bool CleanId(std::string_view word, std::string& id) {
    if (word.size() < 3 || word.size() > 6) return false;
    id.clear();
    for (const char c : word) {
        if (!std::isalnum(static_cast<unsigned char>(c))) return false;
        id.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    return true;
}

// "rrggbb", with or without a leading "0x".
bool ParseRgb(std::string_view word, std::array<std::uint8_t, 3>& rgb) {
    if (word.size() == 8 && word[0] == '0' && (word[1] == 'x' || word[1] == 'X')) word.remove_prefix(2);
    if (word.size() != 6) return false;
    char digits[6];
    for (int i = 0; i < 6; ++i) {
        digits[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(word[i])));
        if (!std::isxdigit(static_cast<unsigned char>(digits[i]))) return false;
    }
    for (int i = 0; i < 3; ++i) rgb[i] = static_cast<std::uint8_t>(Hex(digits[i * 2]) * 16 + Hex(digits[i * 2 + 1]));
    return true;
}

// Between the ID and the colour: white space, "=" or ",".
bool IsSeparator(char c) {
    return std::isspace(static_cast<unsigned char>(c)) || c == '=' || c == ',';
}

// The next word of `rest`, taken off its front; empty when none is left.
// A view into the file's text: a line of junk as long as the file itself
// (256 KiB) costs no copy of it on the menu's small heap.
std::string_view NextWord(std::string_view& rest) {
    std::size_t start = 0;
    while (start < rest.size() && IsSeparator(rest[start])) ++start;
    std::size_t end = start;
    while (end < rest.size() && !IsSeparator(rest[end])) ++end;
    const std::string_view word = rest.substr(start, end - start);
    rest.remove_prefix(end);
    return word;
}

bool StartsWith(const std::string& id, const std::string& prefix) {
    return id.size() >= prefix.size() && id.compare(0, prefix.size(), prefix) == 0;
}

}  // namespace

bool game_case_colour(const std::string& game_id, std::uint8_t (&rgb)[3]) {
    if (game_id.size() != 6) return false;
    char id[6];
    for (int i = 0; i < 6; ++i) id[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(game_id[i])));
    std::size_t lo = 0, hi = kCount;
    while (lo < hi) {
        const std::size_t mid = (lo + hi) / 2;
        const char* entry = kColours + mid * 12;
        const int by = std::memcmp(entry, id, 6);
        if (by == 0) {
            for (int i = 0; i < 3; ++i) rgb[i] = static_cast<std::uint8_t>(Hex(entry[6 + i * 2]) * 16 + Hex(entry[7 + i * 2]));
            return true;
        }
        if (by < 0) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

int parse_case_colour_overrides(const std::string& text, CaseColourOverrides& out) {
    out.clear();
    int skipped = 0;
    std::string_view rest(text);
    while (!rest.empty()) {
        const std::size_t end = rest.find('\n');
        std::string_view line = rest.substr(0, end);
        rest.remove_prefix(end == std::string_view::npos ? rest.size() : end + 1);
        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) line = line.substr(0, comment);
        const std::string_view id_word = NextWord(line);
        if (id_word.empty()) continue;  // a blank line or only a comment
        const std::string_view colour_word = NextWord(line);
        const bool extra = !NextWord(line).empty();
        CaseColourOverride entry;
        if (colour_word.empty() || extra || !CleanId(id_word, entry.id) || !ParseRgb(colour_word, entry.rgb) ||
            out.size() >= kMaxCaseColourOverrides) {
            ++skipped;
            continue;
        }
        out.push_back(entry);
    }
    return skipped;
}

CaseColourSource resolve_case_colour(const std::string& game_id, bool gamecube, const CaseColourOverrides& overrides,
                                     std::uint8_t (&rgb)[3]) {
    std::string id;
    for (const char c : game_id) id.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    // The player's line with the longest ID that this game's starts with.
    const CaseColourOverride* best = nullptr;
    for (const CaseColourOverride& entry : overrides)
        if (StartsWith(id, entry.id) && (best == nullptr || entry.id.size() > best->id.size())) best = &entry;
    if (best != nullptr) {
        for (int i = 0; i < 3; ++i) rgb[i] = best->rgb[i];
        return CaseColourSource::Player;
    }
    if (game_case_colour(id, rgb)) return CaseColourSource::GameTdb;
    for (const Series& series : kSeries) {
        if (!StartsWith(id, series.prefix)) continue;
        for (int i = 0; i < 3; ++i) rgb[i] = series.rgb[i];
        return CaseColourSource::Series;
    }
    // A GameCube case is black, a Wii case white.
    const std::uint8_t console = gamecube ? 0x00 : 0xff;
    rgb[0] = rgb[1] = rgb[2] = console;
    return CaseColourSource::Console;
}

}  // namespace riftwii
