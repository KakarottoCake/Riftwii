// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"
#include "riftwii/sha1.hpp"
#include "riftwii/wadfile.hpp"

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

// The menu's SD or raw FAT32/NTFS USB view, including numbered partitions.
std::unique_ptr<ByteSource> open_wad_source(const std::string& path, std::string& error);

// Select a console-provided title key by verifying the banner's hash.
// Logs only the successful source. The caller clears title_key after use.
bool read_console_wad_banner(const ByteSource& source, const WadMetadata& metadata,
                              std::uint8_t title_key[16], std::vector<std::uint8_t>& banner,
                              std::string& error);
// The same sources, proven on the WAD's smallest content instead of its
// banner, for installing a WAD that has no banner or a large one.
bool read_console_wad_install_key(const ByteSource& source, const WadMetadata& metadata,
                                  std::uint8_t title_key[16], std::string& error);

// Installs the WAD at `path` into the emulated NAND `folder` (sd:/...).
// `progress` gets the fraction done. False with `error` in a plain
// sentence; a content that does not match its hash is not kept. `code`
// is the title's four-letter code once known.
// A null common_key selects the verified OTP / IOS / BootMii route for
// this WAD. Explicit keys are retained for tools and synthetic host tests.
bool install_wad(const std::string& path, const std::string& folder, const std::uint8_t common_key[16],
                 const std::function<void(double)>& progress, std::string& code, std::string& error,
                 std::uint64_t expected_title = 0, const Sha1Digest* source_stamp = nullptr);

}  // namespace riftwii::wii
