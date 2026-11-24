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

// One file an install put on the card, as it was then: its path under the
// card's root ("apps/snes9xgx/boot.dol"), its size and its CRC-32. A file
// that no longer matches was changed since (an app's settings, a save).
struct AppRecordFile {
    std::string path;
    std::uint32_t size = 0;
    std::uint32_t crc = 0;
};

// The store's note of an install, sd:/riftwii/apps/<id>.txt: which file
// it installed, so an update is the list's SHA-256 changing, and every file
// it put on the card, so an update and a removal touch only those.
// `removed`: the app's program files were removed and its saves and
// settings kept; the note stays for them (a later install keeps them too).
struct AppRecord {
    std::string version;
    std::string sha256;
    bool removed = false;
    bool has_files = false;  // a note from a store that did not list them has none
    std::vector<AppRecordFile> files;
};
constexpr std::size_t kAppRecordMaxBytes = 1u << 20;
std::string app_record_text(const AppListing& app, const std::vector<AppRecordFile>& files = {}, bool removed = false);
// False for text that is not a record (the app then counts as put on the
// card some other way). File lines with a bad path are left out.
bool app_record_parse(const std::string& text, AppRecord& out);
// The record's file at `path`, or null.
const AppRecordFile* app_record_find(const AppRecord& record, const std::string& path);

// What the card has where an install would put a file.
enum class AppFileOnCard {
    Absent,       // nothing there
    AsInstalled,  // the file the store put there, unchanged
    Changed,      // the store put it there and it changed since (settings, saves)
    NotOurs,      // a file the store's note does not list
};
// Whether an install or update puts its new file at `path` (under the
// card's root) over what is there. The program (kAppProgramFiles in the
// app's own folder) always is. Otherwise a file changed since the store put it
// there is kept, and so is one the store never put there, except in the
// app's own folder when the app is not the store's (a copy put there by
// hand, which the player chose to replace) or its note lists no files.
bool app_install_replaces(const AppListing& app, const std::string& path, AppFileOnCard on_card, const AppRecord* record);
// The files a removal deletes: those of the record that are unchanged
// (`on_card` says, in the record's order). With no file list, the app's
// boot.dol, boot.elf, meta.xml and icon.png.
std::vector<std::string> app_remove_files(const AppListing& app, const AppRecord* record,
                                          const std::vector<AppFileOnCard>& on_card);
// The program files a remove looks for without a record's file list.
extern const char* const kAppProgramFiles[4];

enum class AppState {
    Get,        // not on the card
    Installed,  // the list's version, installed from the store
    Update,     // installed from the store, and the list has a newer file
    OnCard,     // sd:/apps/<id> is there, not put there by the store
    PcOnly,     // packed in a way the Wii cannot unpack (7z, rar, tar)
};
// `program_exists`: sd:/apps/<id>/boot.dol or boot.elf is there;
// `record`: the store's note, or null when there is none (or it could not
// be read).
AppState app_state(const AppListing& app, bool program_exists, const AppRecord* record);
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
// `keep`, when given, leaves out the apps it says no to (a filter).
std::string app_search_next(const AppListing* apps, std::size_t count, const std::string& typed,
                            bool (*keep)(const AppListing&) = nullptr);

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
std::string app_size_text(std::uint64_t bytes, const char* mb_format = "{1}.{2} MB", const char* kb_format = "{1} KB");

}  // namespace riftwii
