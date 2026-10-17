// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "sharednandfind.hpp"

#include <cstdio>
#include <cstring>

#include "log.hpp"
#include "riftwii/sharednand.hpp"

namespace riftwii::wii {
namespace {

// The settings folder on the card the other loader started from: the SD
// card first, as it looks there first.
const char* const kSettingsFolders[] = {"sd:/apps/usbloader_gx/", "usb:/apps/usbloader_gx/"};

// Lines longer than this are read in pieces; a piece that does not start a
// line is skipped. The other loader's own per-game lines stay below 20 KiB.
constexpr std::size_t kLineBytes = 20 * 1024;
// A settings file bigger than this is not read past it.
constexpr long kMaxFileBytes = 4L * 1024 * 1024;
char g_line[kLineBytes];

// Calls `take` with each whole line of `path` (its end kept), until `take`
// returns true. False when the file does not open.
template <typename Take>
bool each_line(const std::string& path, Take take) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    char* const line = g_line;
    bool at_line_start = true;
    long read = 0;
    while (read < kMaxFileBytes && std::fgets(line, static_cast<int>(kLineBytes), file) != nullptr) {
        const std::size_t length = std::strlen(line);
        read += static_cast<long>(length);
        const bool whole_line_start = at_line_start;
        at_line_start = length > 0 && line[length - 1] == '\n';
        if (whole_line_start && take(std::string(line, length))) break;
    }
    std::fclose(file);
    return true;
}

}  // namespace

SharedNandLookup look_up_shared_nand(const std::string& game_id) {
    std::string configured = kDefaultSharedNand;
    std::string from = "the default folder";
    for (const char* settings : kSettingsFolders) {
        const std::string global = std::string(settings) + "GXGlobal.cfg";
        std::string global_path;
        const bool found = each_line(global, [&](const std::string& line) {
            return shared_nand_global_line(line, global_path);
        });
        if (!found) continue;
        if (!global_path.empty()) {
            configured = global_path;
            from = global;
        }
        // A game's own folder wins over the global one.
        const std::string games = std::string(settings) + "GXGameSettings.cfg";
        std::string game_path;
        each_line(games, [&](const std::string& line) { return shared_nand_game_line(line, game_id, game_path); });
        if (!game_path.empty()) {
            configured = game_path;
            from = games + " (this game's own)";
        }
        break;
    }
    SharedNandLookup found;
    found.configured = configured;
    found.from = from;
    found.usable = shared_nand_folder(configured, found.folder, found.error);
    return found;
}

SharedNandLookup look_up_channel_nand() {
    std::string configured = kDefaultSharedNand;
    std::string from = "the default folder";
    for (const char* settings : kSettingsFolders) {
        const std::string global = std::string(settings) + "GXGlobal.cfg";
        std::string path;
        const bool found = each_line(global, [&](const std::string& line) { return shared_nand_channels_line(line, path); });
        if (!found) continue;
        if (!path.empty()) {
            configured = path;
            from = global;
        }
        break;
    }
    SharedNandLookup found;
    found.configured = configured;
    found.from = from;
    found.usable = shared_nand_folder(configured, found.folder, found.error);
    return found;
}

bool find_shared_nand(const std::string& game_id, std::string& folder, std::string& error) {
    const SharedNandLookup found = look_up_shared_nand(game_id);
    if (!found.usable) {
        error = found.error;
        logf("Saves: emulated NAND folder from %s: %s\n", found.from.c_str(), error.c_str());
        return false;
    }
    folder = found.folder;
    logf("Saves: emulated NAND folder %s, from %s\n", folder.c_str(), found.from.c_str());
    return true;
}

}  // namespace riftwii::wii
