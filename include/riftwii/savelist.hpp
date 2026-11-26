// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/zipread.hpp"

namespace riftwii {

constexpr const char* kSaveListUrl = "https://riftwii.com/saves/saves.json";
constexpr std::size_t kSaveListMaxBytes = 1u << 20;
constexpr std::size_t kSaveListMaxEntries = 512;
constexpr std::uint32_t kSaveDownloadMaxBytes = 32u << 20;
constexpr std::uint32_t kSaveMaxFiles = 512;
constexpr std::uint32_t kSaveMaxUnpackedBytes = 64u << 20;

enum class SavePlatform : std::uint8_t { Wii, GameCube };
enum class SaveFormat : std::uint8_t { Gci, WiiZip, DataBin };

// Fixed-size records: the session cache never grows with network input.
struct SaveListing {
    char id[48]{};
    char game_id[7]{};
    char title[80]{};
    char description[240]{};
    char author[64]{};
    char page[300]{};
    char url[300]{};
    char sha256[65]{};
    std::uint32_t size = 0;
    std::uint32_t files = 0;
    std::uint32_t unpacked_size = 0;
    SavePlatform platform = SavePlatform::Wii;
    SaveFormat format = SaveFormat::DataBin;
};

bool save_game_id_ok(const std::string& id);
bool save_id_ok(const std::string& id);
bool save_url_ok(const std::string& url);
bool save_listing_ok(const SaveListing& save);
// Bad records are skipped; broken JSON or an unknown schema fails closed.
// Duplicate keys are refused, and duplicate save IDs keep only the first.
bool parse_save_list(const char* text, std::size_t bytes, SaveListing* out, std::size_t capacity,
                     std::size_t& count, std::size_t& skipped, std::string& error);

// No region or maker guessing: all six characters must match.
bool save_matches(const SaveListing& save, const std::string& game_id, SavePlatform platform);

// Exactly data/<relative NAND filename>, with the NAND's filename and path
// limits. banner.bin lives inside data too. No other title or content files.
bool save_wii_zip_path(const std::string& name, std::string& relative);
bool save_plan_wii_zip(const SaveListing& save, const std::vector<ZipEntry>& entries,
                       std::vector<std::size_t>& files, std::string& error);
// The disc title ID uses the first four ASCII bytes of the full game ID.
std::string save_wii_title_path(const std::string& game_id);

}  // namespace riftwii
