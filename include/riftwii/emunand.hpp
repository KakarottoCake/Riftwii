// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string>

namespace riftwii {

constexpr const char* kEmuNandMiis = "/shared2/menu/FaceLib/RFL_DB.dat";
constexpr const char* kEmuNandSysconf = "/shared2/sys/SYSCONF";
constexpr const char* kEmuNandSettings = "/title/00000001/00000002/data/setting.txt";
constexpr std::uint64_t kEmuNandMaxSaveFile = 64ULL * 1024 * 1024;
constexpr std::uint64_t kEmuNandMaxSaveBytes = 512ULL * 1024 * 1024;
constexpr unsigned kEmuNandMaxNodes = 4096;
constexpr unsigned kEmuNandMaxDepth = 8;

// Exactly sixteen hexadecimal digits, or two eight-digit halves separated
// by a hyphen. No partial parsing of a folder name is allowed.
bool emunand_parse_title(const std::string& text, std::uint64_t& title);
std::string emunand_title_text(std::uint64_t title);
// The four-character game/channel code, or the full ID for non-text IDs.
std::string emunand_title_code(std::uint64_t title);
bool emunand_system_title(std::uint64_t title);
// Only known Wii title types can be changed. System software is never one.
bool emunand_manageable_title(std::uint64_t title);
// Shared by the manager's UI and delete operation, including ticket-only titles.
bool emunand_delete_protected(std::uint64_t title);

struct EmuNandTitlePaths {
    std::string title;
    std::string data;
    std::string ticket;
};

// Returns relative NAND paths for a title that can be changed. Empty output
// on refusal, including title IDs for IOS or the Wii Menu.
bool emunand_title_paths(std::uint64_t title, EmuNandTitlePaths& paths);

// One on-NAND filename. FAT names with separators, drive prefixes, dots or
// control bytes cannot become paths on the Wii. ISFS allows twelve bytes.
bool emunand_file_name(const std::string& name);
// A data-relative path, including the ISFS path-length limit. Used before
// any real-NAND write, even when the file was found in an SD directory.
bool emunand_save_path(std::uint64_t title, const std::string& relative, std::string& path);

// A checked emulated NAND root plus one of the paths above. The root must
// name a folder below a drive, never the whole SD card or USB partition.
bool emunand_root(const std::string& configured, std::string& folder);

}  // namespace riftwii
