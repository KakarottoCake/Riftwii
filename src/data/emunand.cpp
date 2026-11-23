// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/emunand.hpp"

#include <cstdio>

#include "riftwii/sharednand.hpp"

namespace riftwii {

bool emunand_parse_title(const std::string& text, std::uint64_t& title) {
    if (text.size() != 16 && (text.size() != 17 || text[8] != '-')) return false;
    std::uint64_t parsed = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text.size() == 17 && i == 8) continue;
        const char digit = text[i];
        unsigned value = 0;
        if (digit >= '0' && digit <= '9') value = digit - '0';
        else if (digit >= 'a' && digit <= 'f') value = digit - 'a' + 10;
        else if (digit >= 'A' && digit <= 'F') value = digit - 'A' + 10;
        else return false;
        parsed = (parsed << 4) | value;
    }
    title = parsed;
    return true;
}

std::string emunand_title_text(std::uint64_t title) {
    char text[18];
    std::snprintf(text, sizeof(text), "%08x-%08x", static_cast<unsigned>(title >> 32),
                  static_cast<unsigned>(title));
    return text;
}

bool emunand_system_title(std::uint64_t title) {
    return (title >> 32) == 1;
}

std::string emunand_title_code(std::uint64_t title) {
    std::string code;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const unsigned byte = (title >> shift) & 0xff;
        const bool letter = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z');
        const bool digit = byte >= '0' && byte <= '9';
        if (!letter && !digit) return emunand_title_text(title);
        code += static_cast<char>(byte);
    }
    return code;
}

bool emunand_delete_protected(std::uint64_t title) {
    const std::uint32_t type = title >> 32;
    if (type == 1 || type == 0x00010002 || type == 0x00010008) return true;
    if (type != 0x00010001) return false;

    // Some console tools use the downloadable-channel type. Match the
    // three-letter family, so every region is protected. Public IDs:
    // https://wiibrew.org/wiki/Title_database (title IDs only).
    const std::uint32_t family = static_cast<std::uint32_t>(title) >> 8;
    switch (family) {
        case 0x484142:  // HAB*: Wii Shop.
        case 0x484354:  // HCT*: system transfer.
        case 0x484356:  // HCV*: Wii U Menu.
        case 0x484358:  // HCX*: system transfer.
        case 0x484359:  // HCY*: Wii U system repair.
            return true;
        default:
            return false;
    }
}

bool emunand_manageable_title(std::uint64_t title) {
    if (static_cast<std::uint32_t>(title) == 0) return false;
    switch (title >> 32) {
        case 0x00010000:  // Disc saves.
        case 0x00010001:  // Downloaded channels.
        case 0x00010002:  // Channels such as the Mii Channel, not IOS or the Wii Menu.
        case 0x00010004:  // Disc-installed channels.
        case 0x00010005:  // Downloadable content.
        case 0x00010008:  // Hidden channels.
            return true;
        default:
            return false;
    }
}

bool emunand_title_paths(std::uint64_t title, EmuNandTitlePaths& paths) {
    paths = {};
    if (!emunand_manageable_title(title)) return false;
    const std::string text = emunand_title_text(title);
    const std::string halves = text.substr(0, 8) + "/" + text.substr(9);
    paths.title = "/title/" + halves;
    paths.data = paths.title + "/data";
    paths.ticket = "/ticket/" + halves + ".tik";
    return true;
}

bool emunand_file_name(const std::string& name) {
    if (name.empty() || name.size() > 12 || name == "." || name == "..") return false;
    for (const unsigned char byte : name) {
        if (byte < 32 || byte == 127 || byte == '/' || byte == '\\' || byte == ':' ||
            byte == '*' || byte == '?' || byte == '"' || byte == '<' || byte == '>' || byte == '|') return false;
    }
    // FAT ignores trailing dots/spaces; accepting these would alias names.
    return name.back() != '.' && name.back() != ' ';
}

bool emunand_save_path(std::uint64_t title, const std::string& relative, std::string& path) {
    path.clear();
    EmuNandTitlePaths paths;
    if (!emunand_title_paths(title, paths)) return false;
    if (!relative.empty()) {
        std::size_t start = 0;
        unsigned depth = 0;
        do {
            const std::size_t end = relative.find('/', start);
            const std::string name = relative.substr(start, end == std::string::npos ? end : end - start);
            if (!emunand_file_name(name) || ++depth > kEmuNandMaxDepth) return false;
            if (end == std::string::npos) break;
            start = end + 1;
        } while (true);
    }
    const std::string result = paths.data + (relative.empty() ? "" : "/" + relative);
    if (result.size() >= 64) return false;
    path = result;
    return true;
}

bool emunand_root(const std::string& configured, std::string& folder) {
    folder.clear();
    std::string why;
    std::string checked;
    if (!shared_nand_folder(configured, checked, why)) return false;
    const std::size_t drive = checked.find(":/");
    if (drive == std::string::npos || drive + 2 >= checked.size()) return false;
    // FAT aliases names with trailing dots or spaces. Do not let a checked
    // folder name refer to a different folder when the card is mounted.
    std::size_t start = drive + 2;
    while (start < checked.size()) {
        const std::size_t end = checked.find('/', start);
        const std::size_t last = end == std::string::npos ? checked.size() - 1 : end - 1;
        if (checked[last] == '.' || checked[last] == ' ') return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    folder = checked;
    return true;
}

}  // namespace riftwii
