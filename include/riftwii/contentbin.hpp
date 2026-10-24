// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2020 DarkMatterCore (content.bin layout, as wad2bin and YABDM read and write it)
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/nandchannel.hpp"
#include "riftwii/wadfile.hpp"

// Channels the Wii Menu moved to the SD card (Data Management > Channels >
// SD Card): sd:/private/wii/title/<code>/content.bin. The Wii keeps the
// title's ticket, TMD and save, and deletes its contents; to start one, the
// Wii Menu copies them back for the session. Plain data, host-tested;
// wii/storage/movedchannels.cpp reads the files and stages the contents.
//
// The layout, from the public descriptions in DarkMatterCore's wad2bin
// (which writes the format) and Yet Another BlueDump Mod (which reads it);
// no code taken from either. Every part starts on a 0x40-byte boundary:
//   A  0x640 bytes, the title's ID and banner header, encrypted with the
//      SD card key (RiftWii never needs it: the TMD below says the same);
//   B  the banner's icon, encrypted the same way, of a size only part A
//      gives;
//   C  the "Bk" header (0x70 bytes, padded to 0x80): the console's ID, the
//      TMD's size, the contents' and which of the TMD's contents follow;
//   D  the TMD, plain;
//   E  each included content in TMD order, AES-128-CBC with the console's
//      own PRNG key (OTP 0x68) and the IV its index as two big-endian
//      bytes then zeros, as a WAD's are with the title key;
//   F  the certificates and the signature over C to E.
// Part B's size is the only thing in the way of reading C to E without the
// SD key, so the "Bk" header is looked for past part A instead.
namespace riftwii {

// Where the Wii Menu puts one, below the card's root: "/private/wii/title/WBME/content.bin".
std::string content_bin_path(const std::string& code);

constexpr std::uint64_t kContentBinPartABytes = 0x640;
constexpr std::uint64_t kContentBinBkBytes = 0x80;
// The icon in part B is at most a few hundred KB; the "Bk" header is
// looked for in this much of the file's start.
constexpr std::size_t kContentBinSearchBytes = 1u << 20;

struct ContentBinLayout {
    std::uint64_t bk_offset = 0;
    std::uint32_t console_id = 0;  // the Wii that wrote it
    std::uint64_t tmd_offset = 0, tmd_size = 0;
    std::uint64_t data_offset = 0, data_size = 0;
    // Bit n (lowest first in each byte) is the TMD's nth content record.
    std::array<std::uint8_t, 0x40> included{};
};

// Finds the "Bk" header in `start`, the file's first bytes (up to
// kContentBinSearchBytes), and checks its sizes against the file's. False
// with `error` for anything that is not a moved channel's content.bin.
bool find_content_bin_layout(const std::uint8_t* start, std::size_t size, std::uint64_t file_size,
                             ContentBinLayout& out, std::string& error);

// Whether the TMD's content record at `position` is in the file.
bool content_bin_includes(const ContentBinLayout& layout, std::size_t position);

// Where each included content's encrypted bytes lie, in TMD order; the
// contents left out (shared ones, which stay on the Wii) are not listed.
// False when they run past the file's content data.
bool content_bin_places(const ContentBinLayout& layout, const TmdContents& tmd, std::vector<WadContentPlace>& out,
                        std::string& error);

// The key that decrypts a content.bin's contents, and the console it is
// of: a Wii decrypts only the files it wrote itself.
struct BackupKey {
    std::uint32_t console_id = 0;
    std::uint8_t prng[16] = {};
};

// Another Wii's key from its BootMii backup (keys.bin: a 0x100-byte
// header, then that Wii's OTP), for a card that came from it. False when
// `data` is too short or its OTP is blank.
bool parse_bootmii_keys(const std::uint8_t* data, std::size_t size, BackupKey& out);

}  // namespace riftwii
