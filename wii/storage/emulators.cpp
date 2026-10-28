// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "emulators.hpp"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <set>

#include "log.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/sha256.hpp"
#include "textfile.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kMaxEmulators = 16;
constexpr std::size_t kMaxGames = 2048;
constexpr std::size_t kMaxEntries = 16384;
constexpr std::size_t kMaxFolders = 512;
constexpr unsigned kMaxDepth = 4;

std::string digest_key(const std::string& text) {
    const auto digest = sha256(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    static constexpr char hex[] = "0123456789abcdef";
    std::string key;
    for (const auto byte : digest) {
        key += hex[byte >> 4];
        key += hex[byte & 15];
    }
    return key;
}

std::string values_path(const EmulatorAddon& addon, const std::string& game) {
    return "sd:/riftwii/emulators/" + digest_key(addon.folder + "\n" + game) + ".txt";
}

bool read_manifest(const std::string& path, std::string& text) {
    if (path.rfind("usb:/", 0) != 0) return ReadTextFile(path, text, kEmulatorFileMax);
    std::uint64_t bytes = 0;
    if (!read_usb_range(path, 0, nullptr, 0, bytes)) return false;
    if (bytes == 0 || bytes > kEmulatorFileMax) {
        logf("Emulators: %s was refused because its size is outside 1 to 16384 bytes.\n", path.c_str());
        return false;
    }
    if (!try_resize(text, static_cast<std::size_t>(bytes))) return false;
    return read_usb_range(path, 0, reinterpret_cast<std::uint8_t*>(&text[0]), text.size(), bytes);
}

// stdio on SD, RiftWii's FAT/NTFS reader on USB. Symlinks are never
// followed on host test cards; a folder cannot escape its declared root.
bool list_folder(const std::string& path, std::vector<VolumeEntry>& entries) {
    entries.clear();
    if (path.rfind("usb:/", 0) == 0) return list_usb_folder(path, entries);
    DIR* dir = opendir(path.c_str());
    if (!dir) return false;
    std::size_t examined = 0;
    while (dirent* entry = readdir(dir)) {
        if (++examined > kMaxEntries) {
            logf("Emulators: %s has more than 16384 directory entries; the rest were left out.\n", path.c_str());
            break;
        }
        const std::string name = entry->d_name;
        if (name.empty() || name[0] == '.' || name.size() > 255) continue;
        const std::string child = path + "/" + name;
        if (!emulator_path_valid(child)) continue;
        struct stat info {};
        if (lstat(child.c_str(), &info) != 0 || S_ISLNK(info.st_mode)) continue;
        if (!S_ISDIR(info.st_mode) && !S_ISREG(info.st_mode)) continue;
        const VolumeEntry found{name, S_ISDIR(info.st_mode), static_cast<std::uint64_t>(info.st_size)};
        if (!try_push_back(entries, found)) break;
    }
    closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const VolumeEntry& a, const VolumeEntry& b) { return a.name < b.name; });
    return true;
}

void discover(EmulatorCatalog& out, const std::string& root) {
    std::vector<VolumeEntry> entries;
    if (!list_folder(root, entries)) return;
    std::size_t count = 0;
    for (const auto& entry : entries) {
        if (++count > 1024 || out.addons.size() >= kMaxEmulators) {
            logf("Emulators: the scan reached its limit of 1024 app folders or 16 emulators.\n");
            break;
        }
        if (!entry.is_directory || entry.name.empty() || entry.name[0] == '.' || entry.name.size() > 255) continue;
        const std::string folder = root + "/" + entry.name;
        if (!emulator_path_valid(folder)) continue;
        std::string text;
        if (!read_manifest(folder + "/riftwii-emulator.ini", text)) continue;
        EmulatorAddon addon;
        std::string error;
        std::vector<std::string> warnings;
        if (!parse_emulator_file(text, folder, addon.file, error, warnings)) {
            logf("Emulators: %s was refused. %s\n", folder.c_str(), error.c_str());
            continue;
        }
        for (const auto& warning : warnings) logf("Emulators: %s: %s\n", folder.c_str(), warning.c_str());
        addon.folder = folder;
        out.addons.push_back(std::move(addon));
    }
}

struct WalkBudget {
    std::size_t entries = 0;
    std::size_t folders = 0;
    std::set<std::string> games;
    bool limited = false;
};

void scan_games(EmulatorCatalog& out, std::size_t index, const std::string& folder,
                unsigned depth, WalkBudget& budget) {
    if (budget.entries >= kMaxEntries || budget.folders >= kMaxFolders || out.games.size() >= kMaxGames) {
        budget.limited = true;
        return;
    }
    ++budget.folders;
    std::vector<VolumeEntry> entries;
    if (!list_folder(folder, entries)) return;
    for (const auto& entry : entries) {
        if (++budget.entries > kMaxEntries || out.games.size() >= kMaxGames) {
            budget.limited = true;
            break;
        }
        if (entry.name.empty() || entry.name[0] == '.' || entry.name.size() > 255) continue;
        const std::string path = folder + "/" + entry.name;
        if (!emulator_path_valid(path)) continue;
        if (entry.is_directory) {
            if (depth < kMaxDepth) scan_games(out, index, path, depth + 1, budget);
            else budget.limited = true;
            continue;
        }
        if (!emulator_game_matches(out.addons[index].file, path) || !budget.games.insert(path).second) continue;
        EmulatorGame game;
        game.emulator = index;
        game.path = path;
        game.name = emulator_game_name(path);
        game.key = "emu-" + digest_key(out.addons[index].folder + "\n" + path);
        if (!try_push_back(out.games, std::move(game))) {
            budget.limited = true;
            break;
        }
    }
}

}  // namespace

void ScanEmulators(EmulatorCatalog& out) {
    out = {};
    discover(out, "sd:/apps");
    discover(out, "usb:/apps");
    WalkBudget budget;
    for (std::size_t index = 0; index < out.addons.size(); ++index) {
        budget.games.clear();
        for (const auto& folder : out.addons[index].file.folders) scan_games(out, index, folder, 0, budget);
    }
    if (budget.limited) logf("Emulators: the scan stopped at its entry, folder, depth or game limit.\n");
    logf("Emulators: found %u emulator(s) and %u game(s).\n",
         static_cast<unsigned>(out.addons.size()), static_cast<unsigned>(out.games.size()));
}

void ForgetSdEmulators(EmulatorCatalog& catalog) {
    // App indexes belong to this snapshot. Drop it whole when the SD card
    // leaves, and rebuild it at the next drive scan.
    catalog = {};
}

// A save interrupted before the new file takes its place leaves the old
// settings in the backup. The temporary file is never a committed save.
static bool read_values_file(const std::string& path, std::string& text) {
    struct stat info {};
    if (stat(path.c_str(), &info) == 0 || errno != ENOENT) {
        return ReadTextFile(path, text, kEmulatorFileMax);
    }
    return ReadTextFile(path + ".bak", text, kEmulatorFileMax);
}

EmulatorValues LoadEmulatorValues(const EmulatorAddon& addon, const std::string& game) {
    EmulatorValues values = emulator_defaults(addon.file);
    std::string text;
    if (read_values_file(values_path(addon, ""), text)) read_emulator_values(addon.file, text, values);
    if (!game.empty() && read_values_file(values_path(addon, game), text))
        read_emulator_values(addon.file, text, values);
    return values;
}

bool SaveEmulatorValues(const EmulatorAddon& addon, const std::string& game, const EmulatorValues& values) {
    mkdir("sd:/riftwii", 0777);
    mkdir("sd:/riftwii/emulators", 0777);
    const std::string path = values_path(addon, game);
    const auto defaults = game.empty() ? emulator_defaults(addon.file) : LoadEmulatorValues(addon);
    EmulatorValues overrides;
    for (const auto& value : values) {
        const auto found = defaults.find(value.first);
        if (found != defaults.end() && value.second != found->second) overrides.insert(value);
    }
    const std::string text = write_emulator_values(addon.file, overrides);
    // libfat's rename refuses an existing target. Keep the old settings as
    // a backup until the new file is in place, including after a power loss.
    const std::string temporary = path + ".tmp";
    const std::string backup = path + ".bak";
    if (!WriteWholeFile(temporary, text.data(), text.size())) {
        std::remove(temporary.c_str());
        logf("Emulators: the settings could not be saved to %s.\n", path.c_str());
        return false;
    }
    // A backup beside a present file is left from an older interrupted save
    // and would block the rename below; with the file missing it is the only
    // copy of the settings, and stays until the new file is in place.
    struct stat present {};
    if (stat(path.c_str(), &present) == 0) std::remove(backup.c_str());
    if (std::rename(path.c_str(), backup.c_str()) != 0 && errno != ENOENT) {
        std::remove(temporary.c_str());
        logf("Emulators: the previous settings could not be backed up at %s.\n", path.c_str());
        return false;
    }
    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::rename(backup.c_str(), path.c_str());
        std::remove(temporary.c_str());
        logf("Emulators: the settings could not be saved to %s.\n", path.c_str());
        return false;
    }
    std::remove(backup.c_str());
    return true;
}

bool PrepareEmulatorApp(const EmulatorAddon& addon, const EmulatorGame& game,
                        const EmulatorValues& values, HomebrewApp& app, std::string& error) {
    app = {};
    std::vector<std::string> argv;
    std::string line;
    if (!build_emulator_arguments(addon.file, game.path, values, argv, line, error)) return false;
    app.name = game.name;
    app.folder = addon.folder;
    app.program = addon.file.dol;
    app.path = addon.file.dol;
    app.arguments.assign(argv.begin() + 1, argv.end());
    app.emulator_name = addon.file.name;
    app.emulator_game = game.path;
    app.emulator_command_line = std::move(line);
    return true;
}

}  // namespace riftwii::wii
