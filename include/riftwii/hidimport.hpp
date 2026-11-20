// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace riftwii {

inline constexpr std::array<const char*, 4> kHidImportFiles = {
    "sd:/controller.ini", "usb:/controller.ini",
    "sd:/nintendont/controller.ini", "usb:/nintendont/controller.ini"
};
inline constexpr std::array<const char*, 2> kHidImportFolders = {
    "sd:/controllers", "usb:/controllers"
};

// Only a single VID_PID.ini name is accepted, with either letter case.
bool hid_filename_id(std::string_view name, std::uint32_t& id);
std::string hid_layout_filename(std::uint32_t id);

// Parse first, then check the IDs against a per-device source filename.
// A single controller.ini gets its destination IDs from its contents.
bool validate_hid_import(std::string_view text, std::string_view filename,
                         std::uint32_t& id, std::string& error);

enum class HidImportSave { Saved, Exists, Failed };

// folder is a trusted, existing directory, never a name taken from a file.
// The destination name comes only from the validated mapping. A complete
// temporary copy is closed before the old layout is moved aside.
HidImportSave save_hid_import(const std::string& folder, std::string_view text, bool replace,
                             std::string& destination, std::string& error);

}  // namespace riftwii
