// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/wiiflowplugin.hpp"

#include <algorithm>
#include <cctype>

namespace riftwii {
namespace {

constexpr std::size_t kLineMax = 2048;
constexpr std::size_t kArgumentsMax = 32;
constexpr std::size_t kNameMax = 64;

std::string trim(const std::string& text) {
    std::size_t from = 0, to = text.size();
    while (from < to && std::isspace(static_cast<unsigned char>(text[from]))) ++from;
    while (to > from && std::isspace(static_cast<unsigned char>(text[to - 1]))) --to;
    return text.substr(from, to - from);
}

std::string lower(std::string text) {
    for (char& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}

bool printable(const std::string& text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) >= 0x20 && c != 0x7F; });
}

// A path below a drive's root, as WiiFlow writes them ("roms/snes/",
// "/apps/snes9xgx/boot.dol"): its slashes at either end are dropped, and
// it may not climb out or name another drive.
bool clean_relative(std::string path, std::string& out) {
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    while (!path.empty() && path.back() == '/') path.pop_back();
    if (path.empty() || path.size() > kEmulatorPathMax || !printable(path) || path.find_first_of(":\\") != std::string::npos)
        return false;
    std::size_t from = 0;
    while (true) {
        const auto end = path.find('/', from);
        const std::string part = path.substr(from, end == std::string::npos ? std::string::npos : end - from);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        from = end + 1;
    }
    out = path;
    return true;
}

std::vector<std::string> split_bars(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t from = 0;
    while (from <= text.size()) {
        const auto end = text.find('|', from);
        const std::string part = trim(text.substr(from, end == std::string::npos ? std::string::npos : end - from));
        if (!part.empty()) parts.push_back(part);
        if (end == std::string::npos) break;
        from = end + 1;
    }
    return parts;
}

// The [PLUGIN] section's keys. Other sections and comments are passed over.
bool read_section(const std::string& text, std::vector<std::pair<std::string, std::string>>& keys, std::string& why) {
    bool inside = false;
    std::size_t from = 0;
    while (from < text.size()) {
        const auto end = text.find('\n', from);
        const std::size_t length = (end == std::string::npos ? text.size() : end) - from;
        if (length > kLineMax) {
            why = "a line is longer than 2048 characters";
            return false;
        }
        const std::string line = trim(text.substr(from, length));
        from = end == std::string::npos ? text.size() : end + 1;
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[') {
            inside = lower(line) == "[plugin]";
            continue;
        }
        const auto equals = line.find('=');
        if (!inside || equals == std::string::npos) continue;
        if (keys.size() >= 64) {
            why = "the [PLUGIN] section has more than 64 keys";
            return false;
        }
        keys.emplace_back(trim(line.substr(0, equals)), trim(line.substr(equals + 1)));
    }
    return true;
}

std::string key_value(const std::vector<std::pair<std::string, std::string>>& keys, const char* name) {
    for (const auto& key : keys) {
        if (lower(key.first) == lower(name)) return key.second;
    }
    return "";
}

// WiiFlow's own lists, which a plugin file only switches on in WiiFlow:
// Wii (NWII), GameCube (NGCM), channels (NAND, ENAN) and homebrew (HBRW).
bool wiiflow_own_list(const std::string& magic) {
    const std::string m = lower(magic);
    return m == "4e574949" || m == "4e47434d" || m == "4e414e44" || m == "454e414e" || m.rfind("484252", 0) == 0;
}

// WiiFlow's placeholders as RiftWii's, one argument per |-separated word:
// {device} "sd"/"usb", {path} the folder without the drive, {name} the
// file name, {name_no_ext} without its extension, {loader} the DOL to go
// back to. Anything else in braces refuses the file.
bool convert_argument(std::string word, const std::string& loader, std::string& out) {
    static const std::pair<const char*, const char*> kNames[] = {
        {"{name_no_ext}", "{game_name}"}, {"{name}", "{game_file}"},
        {"{device}", "{game_device}"},    {"{path}", "{game_path}"},
    };
    out.clear();
    for (std::size_t pos = 0; pos < word.size();) {
        if (word[pos] != '{') {
            out += word[pos++];
            continue;
        }
        if (word.compare(pos, 8, "{loader}") == 0) {
            out += loader;
            pos += 8;
            continue;
        }
        bool known = false;
        for (const auto& name : kNames) {
            const std::string from = name.first;
            if (word.compare(pos, from.size(), from) == 0) {
                out += name.second;
                pos += from.size();
                known = true;
                break;
            }
        }
        if (!known) return false;
    }
    return out.size() <= kEmulatorPathMax && printable(out);
}

}  // namespace

WiiflowPluginResult parse_wiiflow_plugin(const std::string& text, const std::string& plugins_dir,
                                         const std::string& loader, WiiflowPlugin& out, std::string& why) {
    out = {};
    why.clear();
    const auto refuse = [&](const std::string& reason) {
        why = reason;
        return WiiflowPluginResult::Refused;
    };
    if (text.empty() || text.size() > kEmulatorFileMax) return refuse("the file must be 1 to 16384 bytes");
    if (!emulator_path_valid(plugins_dir) || !emulator_path_valid(loader))
        return refuse("its folder or RiftWii's own path is not on sd:/ or usb:/");
    std::vector<std::pair<std::string, std::string>> keys;
    if (!read_section(text, keys, why)) return WiiflowPluginResult::Refused;

    const std::string magic = key_value(keys, "magic");
    const std::string dol = key_value(keys, "dolFile");
    const std::string rom_dir = key_value(keys, "romDir");
    if (magic.empty()) return refuse("it has no magic, so WiiFlow doesn't use it either");
    if (wiiflow_own_list(magic) || lower(dol) == "music" || lower(rom_dir).find("scummvm.ini") != std::string::npos) {
        why = "it is one of WiiFlow's own lists, its music player or ScummVM";
        return WiiflowPluginResult::NotAnEmulator;
    }
    if (dol.empty() || rom_dir.empty()) return refuse("it names no dolFile or romDir");

    WiiflowPlugin plugin;
    EmulatorFile& file = plugin.file;
    // WiiFlow's displayname, else the DOL's name without its extension.
    std::string name = key_value(keys, "displayname");
    if (name.size() < 2) {
        name = dol.substr(dol.find_last_of("/:") == std::string::npos ? 0 : dol.find_last_of("/:") + 1);
        if (name.size() > 4 && name[name.size() - 4] == '.') name.resize(name.size() - 4);
    }
    if (name.empty() || name.size() > kNameMax || !printable(name)) return refuse("its displayname is empty or too long");
    file.name = name;
    file.system = name;

    std::string folder;
    if (!clean_relative(rom_dir, folder)) return refuse("its romDir is not a folder below a drive's root");
    file.folders = {"sd:/" + folder, "usb:/" + folder};

    for (std::string type : split_bars(key_value(keys, "fileTypes"))) {
        if (!type.empty() && type.front() == '.') type.erase(type.begin());
        type = lower(type);
        // RiftWii matches the last extension: ".bin.zip" style types are left out.
        const bool plain = !type.empty() && type.size() <= 16 &&
                           std::all_of(type.begin(), type.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
        if (!plain || std::find(file.extensions.begin(), file.extensions.end(), type) != file.extensions.end()) continue;
        if (file.extensions.size() >= kEmulatorExtensionsMax) break;
        file.extensions.push_back(type);
    }
    if (file.extensions.empty()) return refuse("its fileTypes name no extension RiftWii can match");

    const auto words = split_bars(key_value(keys, "arguments"));
    if (words.size() > kArgumentsMax) return refuse("it has more than 32 arguments");
    for (const auto& word : words) {
        std::string converted;
        if (!convert_argument(word, loader, converted)) return refuse("an argument uses a placeholder RiftWii doesn't know: " + word.substr(0, 64));
        file.arguments.push_back(converted);
    }

    // The DOL: a full path, else beside the plugin files, else from each
    // drive's root (WiiFlow's own order).
    if (dol.find(':') != std::string::npos) {
        if (emulator_path_valid(dol)) plugin.dol_candidates.push_back(dol);
    } else {
        std::string relative;
        if (!clean_relative(dol, relative)) return refuse("its dolFile is not a path RiftWii can follow");
        plugin.dol_candidates = {plugins_dir + "/" + relative, "sd:/" + relative, "usb:/" + relative};
    }
    if (plugin.dol_candidates.empty()) return refuse("its dolFile is not on sd:/ or usb:/");

    std::string covers = key_value(keys, "coverFolder");
    if (covers.empty()) covers = magic;
    std::string cover_folder;
    if (clean_relative(covers, cover_folder) && cover_folder.find('/') == std::string::npos) file.wiiflow_covers = cover_folder;

    out = std::move(plugin);
    return WiiflowPluginResult::Emulator;
}

}  // namespace riftwii
