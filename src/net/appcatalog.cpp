// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/appcatalog.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace riftwii {
namespace {

// docs/APPSTORE.md's table, in the store front's order: the biggest
// groups first, "more" last.
constexpr AppCategory kCategories[] = {
    {"games", "Games", "Games made by the homebrew community."},
    {"emulators", "Emulators", "Play games made for older consoles and computers."},
    {"system", "System tools", "Look after the Wii itself: backups, storage and setup."},
    {"gametools", "Game tools", "Save editors, mod tools and other help for games."},
    {"wiimenu", "Wii Menu", "Change how the Wii Menu looks and what it does."},
    {"controllers", "Controllers", "Test, set up and use more kinds of controllers."},
    {"creative", "Creative", "Draw, paint, write and make music."},
    {"toys", "Demos and toys", "Small demos, toys and fun experiments."},
    {"media", "Media", "Watch videos, play music and look at pictures."},
    {"internet", "Internet", "Browse, chat and read on the internet."},
    {"online", "Online services", "Bring the Wii's online play and channels back."},
    {"loaders", "Loaders", "Start games and apps from USB drives and SD cards."},
    {"more", "More", "Everything else."},
};
constexpr std::size_t kCategoryCount = sizeof(kCategories) / sizeof(kCategories[0]);

char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

std::string Lowered(const char* text) {
    std::string out(text);
    for (char& c : out) c = Lower(c);
    return out;
}

bool IsWordBreak(char c) {
    return c == ' ' || c == '-' || c == '_' || c == '.' || c == '(' || c == ')' || c == '/' || c == ',' || c == ':';
}

// Whether `word` starts a word of `text` (both lower case).
bool StartsAWord(const std::string& text, const std::string& word) {
    for (std::size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1)) {
        if (at == 0 || IsWordBreak(text[at - 1])) return true;
    }
    return false;
}

std::vector<std::string> Words(const std::string& typed) {
    std::vector<std::string> words;
    std::string word;
    for (char c : typed) {
        if (c == ' ') {
            if (!word.empty()) words.push_back(word);
            word.clear();
        } else {
            word += Lower(c);
        }
    }
    if (!word.empty()) words.push_back(word);
    return words;
}

bool Fail(std::string& error, const char* why) {
    error = why;
    return false;
}

bool IsHex(const std::string& text) {
    if (text.size() != 64) return false;
    for (char c : text) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

}  // namespace

const AppCategory* app_categories(std::size_t& count) {
    count = kCategoryCount;
    return kCategories;
}

std::size_t app_category_index(const char* key) {
    for (std::size_t i = 0; i + 1 < kCategoryCount; ++i) {
        if (std::strcmp(kCategories[i].key, key) == 0) return i;
    }
    return kCategoryCount - 1;
}

const char* const kAppProgramFiles[4] = {"boot.dol", "boot.elf", "meta.xml", "icon.png"};

namespace {

// A path under the card's root as the store writes one: plain names
// joined by '/', none "." or "..", no backslash, ':' or control characters.
bool RecordPathOk(const std::string& path) {
    if (path.empty() || path.size() > 255 || path.front() == '/' || path.back() == '/') return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        std::size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        for (const char c : part) {
            if (static_cast<unsigned char>(c) < 0x20 || c == '\\' || c == ':') return false;
        }
        start = end + 1;
    }
    return true;
}

// A file line's "<size> <crc> " (decimal, then eight hex digits), by hand:
// the store's plugin has no sscanf. `used` is where the path starts.
bool ReadRecordNumbers(const std::string& value, std::uint32_t& size, std::uint32_t& crc, std::size_t& used) {
    std::size_t at = 0;
    std::uint64_t number = 0;
    while (at < value.size() && value[at] >= '0' && value[at] <= '9' && at < 10) {
        number = number * 10 + static_cast<std::uint64_t>(value[at] - '0');
        ++at;
    }
    if (at == 0 || number > 0xFFFFFFFFull || at >= value.size() || value[at] != ' ') return false;
    size = static_cast<std::uint32_t>(number);
    ++at;
    crc = 0;
    for (int digit = 0; digit < 8; ++digit, ++at) {
        if (at >= value.size()) return false;
        const char c = value[at];
        std::uint32_t nibble;
        if (c >= '0' && c <= '9') nibble = static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') nibble = static_cast<std::uint32_t>(c - 'a' + 10);
        else return false;
        crc = (crc << 4) | nibble;
    }
    if (at >= value.size() || value[at] != ' ') return false;
    used = at + 1;
    return true;
}

// The app's own folder on the card, "apps/<id>/".
std::string OwnFolder(const AppListing& app) { return std::string("apps/") + app.id + "/"; }

}  // namespace

std::string app_record_text(const AppListing& app, const std::vector<AppRecordFile>& files, bool removed) {
    std::string text = std::string("version=") + app.version + "\nsha256=" + app.sha256 + "\n";
    if (removed) text += "removed=1\n";
    // "files=" says the list follows, even when it is empty.
    text += "files=" + std::to_string(files.size()) + "\n";
    for (const AppRecordFile& file : files) {
        char numbers[32];
        std::snprintf(numbers, sizeof(numbers), "%u %08x ", static_cast<unsigned>(file.size), static_cast<unsigned>(file.crc));
        text += std::string("file=") + numbers + file.path + "\n";
    }
    return text;
}

bool app_record_parse(const std::string& text, AppRecord& out) {
    out = AppRecord{};
    if (text.size() > kAppRecordMaxBytes) return false;
    std::size_t at = 0;
    while (at < text.size()) {
        std::size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        at = end + 1;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
        if (key == "version") {
            out.version = value.substr(0, 31);
        } else if (key == "sha256") {
            out.sha256 = value;
        } else if (key == "removed") {
            out.removed = value == "1";
        } else if (key == "files") {
            out.has_files = true;
        } else if (key == "file" && end < text.size() && out.files.size() < kAppMaxFiles) {
            // "<size> <crc> <path>"; a last line without its newline may
            // have been cut off, and is left out.
            std::uint32_t size = 0, crc = 0;
            std::size_t used = 0;
            if (ReadRecordNumbers(value, size, crc, used)) {
                const std::string path = value.substr(used);
                if (RecordPathOk(path) && app_record_find(out, path) == nullptr) out.files.push_back({path, size, crc});
            }
        }
    }
    return IsHex(out.sha256);
}

const AppRecordFile* app_record_find(const AppRecord& record, const std::string& path) {
    for (const AppRecordFile& file : record.files) {
        if (file.path == path) return &file;
    }
    return nullptr;
}

bool app_install_replaces(const AppListing& app, const std::string& path, AppFileOnCard on_card, const AppRecord* record) {
    // The program itself is always the new one: a boot.dol changed since
    // was put there by hand, and keeping it would leave the old version
    // under the new version's note.
    // Case ignored, as app_plan_install does: FAT does not tell BOOT.DOL
    // from boot.dol.
    std::string lower = path;
    for (char& c : lower) c = Lower(c);
    for (const char* name : kAppProgramFiles) {
        std::string program = OwnFolder(app) + name;
        for (char& c : program) c = Lower(c);
        if (lower == program) return true;
    }
    switch (on_card) {
        case AppFileOnCard::Absent:
        case AppFileOnCard::AsInstalled:
            return true;
        case AppFileOnCard::Changed:
            return false;
        case AppFileOnCard::NotOurs:
            break;
    }
    // A file the store did not put there: in a data folder it is the
    // player's (a save, a setting), always kept. In the app's own folder it
    // is replaced only when the store has no list of what it installed.
    const bool own = path.compare(0, OwnFolder(app).size(), OwnFolder(app)) == 0;
    if (!own) return false;
    return record == nullptr || !record->has_files;
}

std::vector<std::string> app_remove_files(const AppListing& app, const AppRecord* record,
                                          const std::vector<AppFileOnCard>& on_card) {
    std::vector<std::string> files;
    if (record == nullptr || !record->has_files) {
        for (const char* name : kAppProgramFiles) files.push_back(OwnFolder(app) + name);
        return files;
    }
    for (std::size_t i = 0; i < record->files.size() && i < on_card.size(); ++i) {
        if (on_card[i] == AppFileOnCard::AsInstalled) files.push_back(record->files[i].path);
    }
    return files;
}

bool app_installable(const AppListing& app) { return app.format == AppFormat::Zip || app.format == AppFormat::Dol; }

AppState app_state(const AppListing& app, bool program_exists, const AppRecord* record) {
    // A note left for the saves of a removed app counts as no install.
    if (record != nullptr && record->removed) record = nullptr;
    if (record != nullptr && program_exists)
        return record->sha256 == app.sha256 ? AppState::Installed : AppState::Update;
    if (program_exists) return AppState::OnCard;
    return app_installable(app) ? AppState::Get : AppState::PcOnly;
}

bool app_matches(const AppListing& app, const std::string& typed) { return app_match_rank(app, typed) > 0; }

int app_match_rank(const AppListing& app, const std::string& typed) {
    const std::vector<std::string> words = Words(typed);
    if (words.empty()) return 1;
    const std::string name = Lowered(app.name), author = Lowered(app.author);
    const std::string category = Lowered(kCategories[app_category_index(app.category)].name);
    int rank = 0;
    for (std::size_t i = 0; i < words.size(); ++i) {
        const std::string& word = words[i];
        int best = 0;
        if (i == 0 && name.compare(0, word.size(), word) == 0) best = 4;
        else if (StartsAWord(name, word)) best = 3;
        else if (name.find(word) != std::string::npos) best = 2;
        else if (StartsAWord(author, word) || StartsAWord(category, word)) best = 1;
        if (best == 0) return 0;  // every word must be found
        rank += best;
    }
    return rank;
}

std::string app_search_next(const AppListing* apps, std::size_t count, const std::string& typed,
                            bool (*keep)(const AppListing&)) {
    static const char kCandidates[] = "abcdefghijklmnopqrstuvwxyz0123456789-'. ";
    std::string next;
    for (const char* c = kCandidates; *c; ++c) {
        if (*c == ' ' && (typed.empty() || typed.back() == ' ')) continue;
        const std::string trial = typed + *c;
        for (std::size_t i = 0; i < count; ++i) {
            if ((keep == nullptr || keep(apps[i])) && app_matches(apps[i], trial)) {
                next += *c;
                break;
            }
        }
    }
    return next;
}

bool app_plan_install(const AppListing& app, const std::vector<ZipEntry>& entries, std::vector<AppInstallStep>& plan,
                      std::string& error) {
    plan.clear();
    if (app.format != AppFormat::Zip) return Fail(error, "the download is not a zip file");
    const std::uint32_t max_files = std::min(app.files, kAppMaxFiles);
    const std::uint64_t max_bytes = std::min(app.unpacked_size, kAppMaxUnpackedBytes);
    // Lower case as `key` is: FAT does not tell WiiMedic from wiimedic.
    std::string own = std::string("apps/") + app.id + "/";
    for (char& c : own) c = Lower(c);
    std::vector<std::string> seen;
    std::uint64_t total = 0;
    bool has_boot = false;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& entry = entries[i];
        if (entry.directory) continue;
        std::string path;
        if (!app_install_path(app, entry.name, path)) continue;  // no rule covers it
        std::string key = path;
        for (char& c : key) c = Lower(c);
        if (std::find(seen.begin(), seen.end(), key) != seen.end())
            return Fail(error, "the zip file holds one file name twice");
        seen.push_back(key);
        total += entry.unpacked_size;
        if (plan.size() + 1 > max_files) return Fail(error, "the zip file holds more files than the app list says");
        if (total > max_bytes) return Fail(error, "the zip file unpacks to more than the app list says");
        if (key == own + "boot.dol" || key == own + "boot.elf") has_boot = true;
        plan.push_back({i, path});
    }
    if (!has_boot) return Fail(error, "the zip file has no boot.dol for the app");
    return true;
}

// {1} the whole megabytes and {2} the tenth ("{1},{2} Mo" in French), or
// {1} the kilobytes: the formats come translated from the plugin.
std::string app_size_text(std::uint64_t bytes, const char* mb_format, const char* kb_format) {
    const auto fill = [](std::string text, const std::string& one, const std::string& two) {
        const std::size_t first = text.find("{1}");
        if (first != std::string::npos) text.replace(first, 3, one);
        const std::size_t second = text.find("{2}");
        if (second != std::string::npos) text.replace(second, 3, two);
        return text;
    };
    if (bytes >= 1000u * 1000u) {
        const std::uint64_t tenths = (bytes + 50000u) / 100000u;
        return fill(mb_format, std::to_string(tenths / 10), std::to_string(tenths % 10));
    }
    return fill(kb_format, std::to_string((bytes + 999u) / 1000u), "");
}

}  // namespace riftwii
