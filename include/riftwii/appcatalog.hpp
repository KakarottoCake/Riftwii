// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/applist.hpp"
#include "riftwii/zipread.hpp"

// What the app store (plugins/appstore, docs/APPSTORE.md) does with the
// list once it is read: its categories, what the card already has of each
// app, the search, and the plan of an install. Plain code, tested on the
// host; the plugin draws and downloads.
namespace riftwii {

// ---- Categories ----

struct AppCategory {
    const char* key;   // the list's key ("emulators")
    const char* name;  // English, for the menu's translation
    const char* about; // one line of what it holds, English, for the translation
};
// The store front's categories in their order. The last, "more", holds
// the apps whose key this RiftWii does not know.
const AppCategory* app_categories(std::size_t& count);
// The index of `key` in app_categories(); the last ("more") for an
// unknown key.
std::size_t app_category_index(const char* key);

// ---- What the card has ----

// The store's note of an install, sd:/riftwii/apps/<id>.txt: which file
// it installed, so an update is the list's SHA-256 changing.
struct AppRecord {
    std::string version;
    std::string sha256;
};
std::string app_record_text(const AppListing& app);
// False for text that is not a record (the app then counts as put on the
// card some other way).
bool app_record_parse(const std::string& text, AppRecord& out);

enum class AppState {
    Get,        // not on the card
    Installed,  // the list's version, installed from the store
    Update,     // installed from the store, and the list has a newer file
    OnCard,     // sd:/apps/<id> is there, not put there by the store
    PcOnly,     // packed in a way the Wii cannot unpack (7z, rar, tar)
};
// `folder_exists`: sd:/apps/<id> is there; `record`: the store's note, or
// null when there is none (or it could not be read).
AppState app_state(const AppListing& app, bool folder_exists, const AppRecord* record);
// Whether the Wii can install the app's download (zip or dol).
bool app_installable(const AppListing& app);

// ---- Search ----

// Whether `app` matches what was typed: each typed word (case ignored) is
// the start of a word of the app's name, author or category, or a part
// of its name ("snes" finds "Snes9x GX", "9x" too).
bool app_matches(const AppListing& app, const std::string& typed);
// How well it matches, to put the best first: a name that starts with
// what was typed beats a word of the name, which beats the author.
int app_match_rank(const AppListing& app, const std::string& typed);
// The characters (lower case, ' ' for Space) that still lead to an app
// when typed next, for the search keyboard's dimmed keys.
std::string app_search_next(const AppListing* apps, std::size_t count, const std::string& typed);

// ---- An install ----

struct AppInstallStep {
    std::size_t entry = 0;  // in the zip's directory
    std::string path;       // under the card's root ("apps/snes9xgx/boot.dol")
};
// Where every entry of `app`'s zip goes (app_install_path), checked as a
// whole before anything is written: within the list's files and
// unpacked_size and the hard caps, no file twice (FAT does not tell A
// from a), and a boot.dol or boot.elf in apps/<id>/. Entries no rule
// covers are left out.
bool app_plan_install(const AppListing& app, const std::vector<ZipEntry>& entries, std::vector<AppInstallStep>& plan,
                      std::string& error);

// "3.1 MB", "640 KB": a download's size for the screen.
std::string app_size_text(std::uint64_t bytes);

}  // namespace riftwii
