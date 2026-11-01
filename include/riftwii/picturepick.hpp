// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "riftwii/sha256.hpp"

namespace riftwii {

// For the cover picker, a Wii or GameCube game's covers from GameTDB's four
// sets: GameTDB (folder cover/), GameTDBSecond (coverB/, #384's second set),
// GameTDBThird (cover2/) and GameTDBFourth (coverB2/). GameTDBMod is the
// cover of a mod tile's own GameTDB entry (#385), from its first set.
enum class PictureSource {
    Title, Screenshot, Boxart, SteamGrid, GameTDB, GameTDBSecond, GameTDBMod, GameTDBThird, GameTDBFourth
};

struct PictureCandidate {
    std::string url;
    PictureSource source = PictureSource::Title;
    Sha256Digest thumbnail_hash{};
    bool thumbnail_hashed = false;
};

// Four sources get equal room. Six native channel textures cost 132 KiB;
// keeping original downloads or a neighbouring page would dwarf that budget.
constexpr std::size_t kPictureCandidatesPerSource = 6;
// A GameTDB set has one cover per region (up to four), the same picture in
// several regions counted until the picker sees they match.
// kPictureCandidatesMax bounds all of them together.
constexpr std::size_t kPictureGameTDBPerSet = 8;
constexpr std::size_t kPictureCandidatesMax = 24;
constexpr std::size_t kPicturePageSize = 6;

std::string picture_libretro_url(PictureSource source, const std::string& system,
                                const std::string& name = "");
bool picture_candidate_valid(const PictureCandidate& candidate);
bool picture_add_libretro(std::vector<PictureCandidate>& candidates, PictureSource source,
                          const std::string& system, const std::string& game,
                          const std::vector<std::string>& names);
bool picture_add_steamgrid(std::vector<PictureCandidate>& candidates, const std::vector<std::string>& urls);
// GameTDB's cover of `game_id` (a game's six capitals or digits, a homebrew
// app's four) in `region` (two capitals), from the set `source` names (one
// of the GameTDB sources). Empty for an ID, region or source that can't be one.
std::string picture_gametdb_url(const std::string& game_id, const std::string& region, PictureSource source);
// That cover as a candidate. False only when picture_gametdb_url is empty.
bool picture_add_gametdb(std::vector<PictureCandidate>& candidates, const std::string& game_id,
                         const std::string& region, PictureSource source);
// Remember only hashes between pages. Remove the later matching candidate
// from the same source, preserving the game's preferred name and region.
// Returns the removed index, or kPictureCandidatesMax if none was removed.
std::size_t picture_record_thumbnail(std::vector<PictureCandidate>& candidates, std::size_t index,
                                     const Sha256Digest& digest);
std::size_t picture_page_count(std::size_t candidates);
// SD only, below RiftWii, with validated components. Callers supply the
// final PNG path, so another picker can use a game's tile key later.
bool picture_save_path_valid(const std::string& path);
std::string picture_pick_marker(const std::vector<std::uint8_t>& png);
bool picture_pick_marker_matches(const std::vector<std::uint8_t>& png, const std::string& marker);

}  // namespace riftwii
