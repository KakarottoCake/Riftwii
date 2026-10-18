// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// Installing .wad files into the emulated NAND on the SD card that
// USB Loader GX keeps its channels in (sharednandfind.hpp's
// look_up_channel_nand), where RiftWii lists and starts them
// (nandchannels.hpp). The Wii's own NAND is never written. The contents
// are decrypted with the title key from the WAD's ticket, which the
// console's common key decrypts (riftwii/wadfile.hpp).
namespace riftwii::wii {

// Where players put WAD files, as other loaders and installers expect;
// "wads" too, which a tester named theirs and found none listed.
constexpr const char* kWadFolders[] = {"sd:/wad", "sd:/wads"};

// The .wad files in kWadFolders, sorted by name.
std::vector<std::string> list_wad_files();

// Installs the WAD at `path` into the emulated NAND `folder` (sd:/...).
// `progress` gets the fraction done. False with `error` in a plain
// sentence; a content that does not match its hash is not kept. `code`
// is the title's four-letter code once known.
bool install_wad(const std::string& path, const std::string& folder, const std::uint8_t common_key[16],
                 const std::function<void(double)>& progress, std::string& code, std::string& error);

}  // namespace riftwii::wii
