// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: USB Loader GX contributors <https://github.com/wiidev/usbloadergx>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/sharednand.hpp"

namespace riftwii {
namespace {

bool blank(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// Spaces, tabs and line ends off both ends.
std::string trimmed(const std::string& text) {
    std::size_t begin = 0;
    while (begin < text.size() && blank(text[begin])) ++begin;
    std::size_t end = text.size();
    while (end > begin && blank(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

// The line without a UTF-8 byte order mark, which an editor may put at
// the start of the file (and so of its first line).
std::string without_bom(const std::string& line) {
    if (line.compare(0, 3, "\xEF\xBB\xBF") == 0) return line.substr(3);
    return line;
}

}  // namespace

bool shared_nand_global_line(const std::string& raw_line, std::string& path) {
    const std::string line = trimmed(without_bom(raw_line));
    if (line.empty() || line[0] == '#') return false;
    const std::size_t equals = line.find('=');
    if (equals == std::string::npos) return false;
    if (trimmed(line.substr(0, equals)) != "NandEmuPath") return false;
    path = trimmed(line.substr(equals + 1));
    return true;
}

bool shared_nand_game_line(const std::string& raw_line, const std::string& game_id, std::string& path) {
    const std::string line = trimmed(without_bom(raw_line));
    const std::string prefix = "game:" + game_id;
    if (game_id.empty() || line.compare(0, prefix.size(), prefix) != 0) return false;
    // "game:RMCE01 = ...": the ID ends at a space or the "=".
    if (line.size() > prefix.size() && !blank(line[prefix.size()]) && line[prefix.size()] != '=') return false;
    std::size_t field = line.find('=', prefix.size());
    if (field == std::string::npos) return false;
    // Fields are "name:value;" one after another.
    while (field < line.size()) {
        const std::size_t start = field + 1;
        const std::size_t colon = line.find(':', start);
        if (colon == std::string::npos) return false;
        std::size_t semicolon = line.find(';', colon + 1);
        if (semicolon == std::string::npos) semicolon = line.size();
        const std::string name = trimmed(line.substr(start, colon - start));
        if (name == "NandEmuPath") {
            path = trimmed(line.substr(colon + 1, semicolon - colon - 1));
            return !path.empty();
        }
        field = semicolon;
    }
    return false;
}

bool shared_nand_folder(const std::string& configured, std::string& folder, std::string& why) {
    std::string path = configured;
    while (path.size() > 4 && path.back() == '/') path.pop_back();
    if (path.compare(0, 3, "usb") == 0) {
        why = "the emulated NAND folder " + configured + " is on the USB drive; RiftWii keeps saves only on the SD card";
        return false;
    }
    if (path.compare(0, 4, "sd:/") != 0 || path.size() <= 4) {
        why = "the emulated NAND folder \"" + configured + "\" is not a folder of the SD card";
        return false;
    }
    if (path.size() > kSharedNandMaxPath) {
        why = "the emulated NAND folder " + configured + " is too long a path for d2x";
        return false;
    }
    // Each part between slashes: not empty, not "." or "..", and only
    // characters a FAT name holds. The folder must stay where it says.
    std::size_t part_start = 4;
    while (part_start <= path.size()) {
        std::size_t part_end = path.find('/', part_start);
        if (part_end == std::string::npos) part_end = path.size();
        const std::string part = path.substr(part_start, part_end - part_start);
        if (part.empty() || part == "." || part == "..") {
            why = "the emulated NAND folder " + configured + " is not a plain folder path";
            return false;
        }
        for (const char c : part) {
            const unsigned char u = static_cast<unsigned char>(c);
            if (u < 0x20 || u >= 0x7F || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
                c == '>' || c == '|') {
                why = "the emulated NAND folder " + configured + " has a character a folder name cannot have";
                return false;
            }
        }
        part_start = part_end + 1;
    }
    folder = path;
    return true;
}

}  // namespace riftwii
