// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ghcheats.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace riftwii {
namespace {

constexpr const char kZipPrefix[] = "text_wii_";
constexpr const char kZipSuffix[] = ".zip";
constexpr std::size_t kDateDigits = 8;
constexpr std::size_t kMaxNameBytes = 200;
// How much of an entry's start is looked at for the ID line.
constexpr std::size_t kHeadBytes = 64;
constexpr const char kIndexHeader[] = "RiftWii GameHacking.org index 1";

bool Fail(std::string& error, const char* why) {
    error = why;
    return false;
}

bool all_digits(const std::string& text, std::size_t at, std::size_t count) {
    if (at + count > text.size()) return false;
    for (std::size_t i = 0; i < count; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(text[at + i]))) return false;
    }
    return true;
}

// "text_wii_YYYYMMDD.zip" with a real-looking date: the date as a number
// (20260927) for comparing, 0 when `name` is not one.
unsigned long zip_name_date(const std::string& name) {
    const std::size_t prefix = sizeof(kZipPrefix) - 1;
    const std::size_t suffix = sizeof(kZipSuffix) - 1;
    if (name.size() != prefix + kDateDigits + suffix) return 0;
    if (name.compare(0, prefix, kZipPrefix) != 0) return 0;
    if (name.compare(prefix + kDateDigits, suffix, kZipSuffix) != 0) return 0;
    if (!all_digits(name, prefix, kDateDigits)) return 0;
    const unsigned long date = std::strtoul(name.substr(prefix, kDateDigits).c_str(), nullptr, 10);
    const unsigned long month = date / 100 % 100;
    const unsigned long day = date % 100;
    if (month < 1 || month > 12 || day < 1 || day > 31) return 0;
    return date;
}

bool has_suffix_nocase(const std::string& text, const char* suffix) {
    const std::size_t n = std::char_traits<char>::length(suffix);
    if (text.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        const char c = text[text.size() - n + i];
        if (std::tolower(static_cast<unsigned char>(c)) != suffix[i]) return false;
    }
    return true;
}

bool id_char_ok(char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

bool id_ok(const std::string& id) {
    if (id.size() != 4 && id.size() != 6) return false;
    return std::all_of(id.begin(), id.end(), id_char_ok);
}

// The entries that are cheat files worth reading: not folders, a name
// ending in .txt, and not over the size bound.
bool wanted(const ZipEntry& entry, const GameHackingLimits& limits) {
    return !entry.directory && has_suffix_nocase(entry.name, ".txt") && entry.unpacked_size <= limits.entry_unpacked;
}

}  // namespace

bool gamehacking_zip_name_ok(const std::string& name) { return zip_name_date(name) != 0; }

bool gamehacking_newest_wii_zip(const std::string& listing, std::string& name, std::string& error) {
    if (listing.empty()) return Fail(error, "GameHacking.org sent an empty listing");
    const std::size_t length = sizeof(kZipPrefix) - 1 + kDateDigits + sizeof(kZipSuffix) - 1;
    unsigned long best_date = 0;
    std::string best;
    std::size_t at = 0;
    while ((at = listing.find(kZipPrefix, at)) != std::string::npos) {
        // A link target, not a word somewhere in the page: after a quote or
        // a slash, and ending the address (so "...zip.bak" is not one).
        const char before = at > 0 ? listing[at - 1] : '\0';
        const char after = at + length < listing.size() ? listing[at + length] : '\0';
        const bool anchored = (before == '"' || before == '\'' || before == '/') &&
                              (after == '"' || after == '\'' || after == '?' || after == '#');
        const std::string candidate = listing.substr(at, length);
        const unsigned long date = anchored ? zip_name_date(candidate) : 0;
        if (date > best_date) {
            best_date = date;
            best = candidate;
        }
        at += 1;
    }
    if (best.empty()) return Fail(error, "GameHacking.org's listing has no text_wii zip");
    name = best;
    return true;
}

bool gamehacking_stale(std::int64_t fetched, std::int64_t now) {
    if (fetched <= 0) return true;
    if (now < fetched) return false;
    return now - fetched > kGameHackingMaxAge;
}

bool gamehacking_entry_name_ok(const std::string& name) {
    if (name.empty() || name.size() > kMaxNameBytes) return false;
    if (name[0] == '.' || name.find("..") != std::string::npos) return false;
    for (char c : name) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7F || c == '/' || c == '\\') return false;
    }
    return true;
}

bool gamehacking_text_id(const char* text, std::size_t size, std::string& id) {
    std::size_t at = 0;
    if (size >= 3 && text[0] == '\xEF' && text[1] == '\xBB' && text[2] == '\xBF') at = 3;
    std::string line;
    while (at < size && text[at] != '\n' && text[at] != '\r') line.push_back(text[at++]);
    while (!line.empty() && line.back() == ' ') line.pop_back();
    if (!id_ok(line)) return false;
    id = line;
    return true;
}

bool gamehacking_build_index(const std::uint8_t* data, std::size_t size, const GameHackingLimits& limits,
                             ZipInflater inflate, GameHackingIndex& index, std::string& error) {
    index.entry.clear();
    index.left_out = 0;
    if (size > limits.zip_bytes) return Fail(error, "the GameHacking.org zip is bigger than RiftWii takes (4 MB)");
    std::vector<ZipEntry> entries;
    if (!zip_read_directory(data, size, entries, error, limits.entries)) return false;
    std::uint64_t total = 0;
    for (const ZipEntry& entry : entries) {
        total += entry.unpacked_size;
        if (total > limits.total_unpacked)
            return Fail(error, "the GameHacking.org zip unpacks to more than RiftWii takes (16 MB)");
        if (!entry.directory && !gamehacking_entry_name_ok(entry.name))
            return Fail(error, "the GameHacking.org zip holds a name that is not a plain file name");
    }
    std::map<std::string, std::uint32_t> sizes;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& entry = entries[i];
        if (entry.directory) continue;
        if (!wanted(entry, limits)) {
            ++index.left_out;
            continue;
        }
        // Only the start of the entry is kept, but the whole of it is
        // unpacked and its CRC-32 checked, so a damaged file is found now.
        std::string head;
        const ZipSink keep_head = [&](const std::uint8_t* bytes, std::size_t count) {
            if (head.size() < kHeadBytes)
                head.append(reinterpret_cast<const char*>(bytes), std::min(count, kHeadBytes - head.size()));
            return true;
        };
        if (!zip_extract(data, size, entry, inflate, keep_head, error)) return false;
        std::string id;
        if (!gamehacking_text_id(head.data(), head.size(), id)) {
            ++index.left_out;
            continue;
        }
        const auto have = sizes.find(id);
        if (have != sizes.end() && have->second >= entry.unpacked_size) {
            ++index.left_out;
            continue;
        }
        // A bigger file for a game replaces the smaller one kept before.
        if (have != sizes.end()) ++index.left_out;
        sizes[id] = entry.unpacked_size;
        index.entry[id] = i;
    }
    return true;
}

std::string gamehacking_index_text(const GameHackingIndex& index) {
    std::string text = std::string(kIndexHeader) + "\nzip " + index.zip + "\nfetched " + std::to_string(index.fetched) +
                       "\nentries " + std::to_string(index.entry.size()) + "\n";
    for (const auto& item : index.entry) text += item.first + " " + std::to_string(item.second) + "\n";
    return text;
}

bool gamehacking_parse_index(const std::string& text, GameHackingIndex& index, std::string& error) {
    index = GameHackingIndex{};
    std::vector<std::string> lines;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t stop = text.find('\n', at);
        if (stop == std::string::npos) stop = text.size();
        std::string line = text.substr(at, stop - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
        at = stop + 1;
        if (lines.size() > 100000) return Fail(error, "the GameHacking.org index is too long");
    }
    if (lines.size() < 4 || lines[0] != kIndexHeader)
        return Fail(error, "the GameHacking.org index is not in RiftWii's format");
    if (lines[1].compare(0, 4, "zip ") != 0 || !gamehacking_zip_name_ok(lines[1].substr(4)))
        return Fail(error, "the GameHacking.org index names no zip");
    index.zip = lines[1].substr(4);
    if (lines[2].compare(0, 8, "fetched ") != 0 || lines[2].size() == 8 || lines[2].size() > 8 + 12 ||
        !all_digits(lines[2], 8, lines[2].size() - 8))
        return Fail(error, "the GameHacking.org index has no date");
    index.fetched = std::strtoll(lines[2].c_str() + 8, nullptr, 10);
    if (lines[3].compare(0, 8, "entries ") != 0 || lines[3].size() == 8 || lines[3].size() > 8 + 9 ||
        !all_digits(lines[3], 8, lines[3].size() - 8))
        return Fail(error, "the GameHacking.org index has no count");
    const std::size_t count = std::strtoul(lines[3].c_str() + 8, nullptr, 10);
    if (lines.size() != 4 + count) return Fail(error, "the GameHacking.org index is not whole");
    for (std::size_t i = 0; i < count; ++i) {
        const std::string& line = lines[4 + i];
        const std::size_t space = line.find(' ');
        if (space == std::string::npos) return Fail(error, "the GameHacking.org index is damaged");
        const std::string id = line.substr(0, space);
        const std::size_t digits = line.size() - space - 1;
        if (!id_ok(id) || digits == 0 || digits > 9 || !all_digits(line, space + 1, digits))
            return Fail(error, "the GameHacking.org index is damaged");
        index.entry[id] = std::strtoul(line.c_str() + space + 1, nullptr, 10);
    }
    if (index.entry.size() != count) return Fail(error, "the GameHacking.org index lists a game twice");
    return true;
}

std::string gamehacking_source_text(const std::string& zip) { return std::string(kGameHackingName) + " " + zip + "\n"; }

bool gamehacking_source_parse(const std::string& text, std::string& zip) {
    const std::string name = kGameHackingName;
    if (text.compare(0, name.size() + 1, name + " ") != 0) return false;
    std::size_t stop = text.find_first_of("\r\n", name.size() + 1);
    const std::string candidate = text.substr(name.size() + 1, stop == std::string::npos ? std::string::npos : stop - name.size() - 1);
    if (!gamehacking_zip_name_ok(candidate)) return false;
    zip = candidate;
    return true;
}

bool gamehacking_read_cheats(const std::uint8_t* data, std::size_t size, const GameHackingIndex& index,
                             const std::string& id, const GameHackingLimits& limits, ZipInflater inflate,
                             std::string& text, std::string& error) {
    text.clear();
    const auto found = index.entry.find(id);
    if (found == index.entry.end()) return Fail(error, "GameHacking.org has no cheats for this game");
    if (size > limits.zip_bytes) return Fail(error, "the cached GameHacking.org zip is bigger than RiftWii takes");
    std::vector<ZipEntry> entries;
    if (!zip_read_directory(data, size, entries, error, limits.entries)) return false;
    if (found->second >= entries.size()) return Fail(error, "the GameHacking.org index does not match its zip");
    const ZipEntry& entry = entries[found->second];
    if (!wanted(entry, limits) || !gamehacking_entry_name_ok(entry.name))
        return Fail(error, "the GameHacking.org index does not match its zip");
    const ZipSink collect = [&](const std::uint8_t* bytes, std::size_t count) {
        text.append(reinterpret_cast<const char*>(bytes), count);
        return true;
    };
    if (!zip_extract(data, size, entry, inflate, collect, error)) {
        text.clear();
        return false;
    }
    std::string first;
    if (!gamehacking_text_id(text.data(), text.size(), first) || first != id) {
        text.clear();
        return Fail(error, "the GameHacking.org index does not match its zip");
    }
    return true;
}

}  // namespace riftwii
