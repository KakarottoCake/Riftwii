// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The list of homebrew apps RiftWii can download (docs/APPSTORE.md):
// apps.json on riftwii.github.io, schema 1,
//
//   {"schema":1,"generated":"YYYY-MM-DD","apps":[{"id","name","author",
//    "version","released","category","description","license","page",
//    "platforms","controllers","icon","newer","data_folders",
//    "download":{"url","format","size","sha256","files","unpacked_size"},
//    "install":[{"from","to"}]}]}
//
// RiftWii hosts none of the apps: each "download" is the app's own release
// file, pinned by its SHA-256, so what is installed is the version the list
// approved, wherever it is served from. The list comes from the internet
// and is read as untrusted: its size, its app count, every string and
// every count are bounded, and an app that breaks a rule is left out,
// never trusted. Records are fixed-size so the Wii keeps the whole list in
// one block of MEM2, as the theme list does (themelist.hpp).
namespace riftwii {

constexpr const char* kAppListUrl = "https://riftwii.github.io/apps/apps.json";
constexpr const char* kAppSiteUrl = "https://riftwii.github.io/";

constexpr std::size_t kAppListMaxBytes = 2u << 20;
constexpr std::size_t kAppListMaxApps = 1000;
constexpr std::uint32_t kAppDownloadMaxBytes = 128u << 20;
constexpr std::uint32_t kAppMaxFiles = 8192;
constexpr std::uint32_t kAppMaxUnpackedBytes = 256u << 20;
constexpr std::size_t kAppMaxInstallRules = 6;
constexpr std::size_t kAppMaxDataFolders = 4;
constexpr std::size_t kAppIconMaxBytes = 64u << 10;

// How the download is packed. Only Zip and Dol install on the Wii; the rest
// are listed so the store can show the app and where to get it on a PC.
enum class AppFormat : std::uint8_t { Zip, Dol, SevenZip, Rar, Tar, TarBz2, TarGz };

// Which consoles an app runs on (bits of AppListing::platforms).
constexpr std::uint8_t kAppOnWii = 1;
constexpr std::uint8_t kAppOnVWii = 2;
constexpr std::uint8_t kAppOnWiiMini = 4;

// Which controllers an app can be used with (bits of AppListing::controllers),
// as its manifest says; 0 when the list does not know. "keyboard" is a USB
// keyboard.
constexpr std::uint8_t kAppWiiRemote = 1;
constexpr std::uint8_t kAppNunchuk = 2;
constexpr std::uint8_t kAppClassic = 4;
constexpr std::uint8_t kAppGameCube = 8;
constexpr std::uint8_t kAppKeyboard = 16;
// Whether an app with these controllers can be used without a Wii Remote:
// it works with a Classic Controller or a GameCube controller.
inline bool app_needs_no_wii_remote(std::uint8_t controllers) {
    return (controllers & (kAppClassic | kAppGameCube)) != 0;
}

// One rule of an app's install: zip entries under `from` (a folder, ending
// in '/'; "" for the download's top level; or one file) go under `to` on
// the card. `to` starts with "apps/<id>/" or one of the app's data folders.
struct AppInstallRule {
    char from[64];
    char to[64];
};

struct AppListing {
    char id[48];            // the app's folder: sd:/apps/<id> (app_id_ok)
    char name[64];          // for the menu: printable, no control characters
    char author[64];
    char version[32];
    char released[11];      // YYYY-MM-DD, or empty
    char category[16];      // a category key (app_category_ok); unknown keys show under "More"
    char description[160];
    char license[32];       // may be empty
    char page[200];         // the app's own page, for a QR code (https://)
    char icon[200];         // https://riftwii.github.io/..., may be empty
    char newer[48];         // the id of an app that does the same job better, or empty
    char url[300];          // the download (app_download_url_ok)
    char sha256[65];        // of the download, 64 lower-case hex digits
    char data_folders[kAppMaxDataFolders][32];  // card folders the app keeps its data in
    AppInstallRule install[kAppMaxInstallRules];
    // No initializers, so a block of these is plain memory a caller zeroes.
    std::uint32_t size;           // the download's bytes
    std::uint32_t files;          // entries it installs
    std::uint32_t unpacked_size;  // bytes it installs
    std::uint8_t install_count;
    std::uint8_t data_folder_count;
    std::uint8_t platforms;       // kAppOn* bits
    std::uint8_t controllers;     // kApp* controller bits, 0: not known
    AppFormat format;
};

// Whether `id` may be an app's id, and so its folder under sd:/apps: one
// plain name (no '/', '\', leading dot or space, control character, "." or
// ".."), letters, digits and "-_ .+()!" only, at most 47 bytes.
bool app_id_ok(const std::string& id);
// Whether `key` is a category key: 1 to 15 lower-case letters.
bool app_category_ok(const std::string& key);
// Whether `name` may be one of an app's data folders on the card: an id-like
// plain name, and none of the folders the Wii, RiftWii or other homebrew
// own (apps, riftwii, private, title, shared1, wbfs, games, ...).
bool app_data_folder_ok(const std::string& name);
// Whether `url` is an address RiftWii may download an app from: https://
// or http://, a host name (no user info, port or IP address), a path of
// plain URL characters, at most 299 bytes. The SHA-256 makes the file what
// the list approved, so neither the host nor the connection need be
// trusted: old apps live on plain-http sites.
bool app_download_url_ok(const std::string& url);
// A page to show as a QR code: the same, https:// only.
bool app_page_url_ok(const std::string& url);
// An icon comes from RiftWii's own site: https://riftwii.github.io/...
bool app_icon_url_ok(const std::string& url);

// Reads the list into `out` (room for `max` records). `count` is how many
// apps it holds; `skipped` how many entries were left out for breaking a
// rule. False with `error` when the text is not a schema 1 list at all or
// is over the size cap.
bool parse_app_list(const char* text, std::size_t text_size, AppListing* out, std::size_t max, std::size_t& count,
                    std::size_t& skipped, std::string& error);
bool parse_app_list(const std::string& text, AppListing* out, std::size_t max, std::size_t& count,
                    std::size_t& skipped, std::string& error);

// Where a zip entry of `app`'s download goes on the card, as a path under
// the card's root ("apps/snes9xgx/boot.dol"). False when no install rule
// covers the entry (it is not installed), or when the result would leave
// the app's own folders: every part of the entry's path below its rule is
// a plain name, so nothing climbs out.
bool app_install_path(const AppListing& app, const std::string& zip_entry, std::string& card_path);

}  // namespace riftwii
