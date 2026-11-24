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
    {"games", "Games"},
    {"emulators", "Emulators"},
    {"system", "System tools"},
    {"gametools", "Game tools"},
    {"wiimenu", "Wii Menu"},
    {"controllers", "Controllers"},
    {"creative", "Creative"},
    {"toys", "Demos and toys"},
    {"media", "Media"},
    {"internet", "Internet"},
    {"online", "Online services"},
    {"loaders", "Loaders"},
    {"more", "More"},
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

std::string app_record_text(const AppListing& app) {
    return std::string("version=") + app.version + "\nsha256=" + app.sha256 + "\n";
}

bool app_record_parse(const std::string& text, AppRecord& out) {
    out = AppRecord{};
    if (text.size() > 512) return false;
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
        if (key == "version") out.version = value.substr(0, 31);
        else if (key == "sha256") out.sha256 = value;
    }
    return IsHex(out.sha256);
}

bool app_installable(const AppListing& app) { return app.format == AppFormat::Zip || app.format == AppFormat::Dol; }

AppState app_state(const AppListing& app, bool folder_exists, const AppRecord* record) {
    if (record != nullptr && folder_exists)
        return record->sha256 == app.sha256 ? AppState::Installed : AppState::Update;
    if (folder_exists) return AppState::OnCard;
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

std::string app_search_next(const AppListing* apps, std::size_t count, const std::string& typed) {
    static const char kCandidates[] = "abcdefghijklmnopqrstuvwxyz0123456789-'. ";
    std::string next;
    for (const char* c = kCandidates; *c; ++c) {
        if (*c == ' ' && (typed.empty() || typed.back() == ' ')) continue;
        const std::string trial = typed + *c;
        for (std::size_t i = 0; i < count; ++i) {
            if (app_matches(apps[i], trial)) {
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
    const std::string own = std::string("apps/") + app.id + "/";
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

std::string app_size_text(std::uint64_t bytes) {
    char text[32];
    if (bytes >= 1000u * 1000u) {
        const std::uint64_t tenths = (bytes + 50000u) / 100000u;
        std::snprintf(text, sizeof(text), "%u.%u MB", static_cast<unsigned>(tenths / 10), static_cast<unsigned>(tenths % 10));
    } else {
        std::snprintf(text, sizeof(text), "%u KB", static_cast<unsigned>((bytes + 999u) / 1000u));
    }
    return text;
}

}  // namespace riftwii
