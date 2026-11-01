// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/picturepick.hpp"

#include <algorithm>

#include "riftwii/checkedalloc.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/emulatorart.hpp"
#include "riftwii/sha256.hpp"

namespace riftwii {
namespace {

int hex_digit(char byte) {
    if (byte >= '0' && byte <= '9') return byte - '0';
    if (byte >= 'A' && byte <= 'F') return byte - 'A' + 10;
    return -1;
}

bool decode_component(const std::string& encoded, std::string& decoded) {
    if (encoded.size() > kEmulatorArtNameMax * 3) return false;
    decoded.clear();
    for (std::size_t at = 0; at < encoded.size(); ++at) {
        char byte = encoded[at];
        if (byte == '%') {
            if (at + 2 >= encoded.size()) return false;
            const int high = hex_digit(encoded[at + 1]);
            const int low = hex_digit(encoded[at + 2]);
            if (high < 0 || low < 0) return false;
            byte = static_cast<char>((high << 4) | low);
            at += 2;
        }
        decoded += byte;
    }
    return emulator_art_component(decoded);
}

bool gametdb_id(const std::string& id) {
    if (id.size() != 4 && id.size() != 6) return false;
    return std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); });
}

bool gametdb_region(const std::string& region) {
    return region.size() == 2 && std::all_of(region.begin(), region.end(), [](char c) { return c >= 'A' && c <= 'Z'; });
}

// A GameTDB cover URL taken apart and built again from checked parts, as
// for libretro's: anything else (another host, folder or file) is refused.
bool gametdb_url_valid(const PictureCandidate& candidate) {
    const bool second = candidate.source == PictureSource::GameTDBSecond;
    const std::string prefix = second ? "http://art.gametdb.com/wii/coverB/" : "http://art.gametdb.com/wii/cover/";
    if (candidate.url.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string rest = candidate.url.substr(prefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string::npos || rest.size() < slash + 5 || rest.compare(rest.size() - 4, 4, ".png") != 0) return false;
    const std::string region = rest.substr(0, slash);
    const std::string id = rest.substr(slash + 1, rest.size() - slash - 5);
    return gametdb_region(region) && gametdb_id(id) && cover_url(region, id, second) == candidate.url;
}

std::size_t source_count(const std::vector<PictureCandidate>& candidates, PictureSource source) {
    return static_cast<std::size_t>(std::count_if(candidates.begin(), candidates.end(), [&](const PictureCandidate& item) {
        return item.source == source;
    }));
}

}  // namespace

std::string picture_libretro_url(PictureSource source, const std::string& system, const std::string& name) {
    std::string url = libretro_cover_url(system, name);
    const auto folder = url.find("/Named_Boxarts/");
    if (folder == std::string::npos) return "";
    switch (source) {
        case PictureSource::Title:
            url.replace(folder, 15, "/Named_Titles/");
            break;
        case PictureSource::Screenshot:
            url.replace(folder, 15, "/Named_Snaps/");
            break;
        case PictureSource::Boxart: break;
        case PictureSource::SteamGrid:
        case PictureSource::GameTDB:
        case PictureSource::GameTDBSecond: return "";
        default: return "";
    }
    return url;
}

bool picture_candidate_valid(const PictureCandidate& candidate) {
    if (candidate.url.size() > 2048) return false;
    if (candidate.source == PictureSource::SteamGrid) return steamgrid_image_url(candidate.url);
    if (candidate.source == PictureSource::GameTDB || candidate.source == PictureSource::GameTDBSecond)
        return gametdb_url_valid(candidate);
    const std::string prefix = "https://thumbnails.libretro.com/";
    if (candidate.url.compare(0, prefix.size(), prefix) != 0) return false;
    const auto folder = candidate.url.find('/', prefix.size());
    if (folder == std::string::npos || folder == prefix.size()) return false;
    const auto filename = candidate.url.find_last_of('/');
    if (filename <= folder || filename + 1 >= candidate.url.size()) return false;
    std::string system, name;
    if (!decode_component(candidate.url.substr(prefix.size(), folder - prefix.size()), system) ||
        !decode_component(candidate.url.substr(filename + 1), name)) return false;
    // Rebuild the canonical URL from decoded, checked components. This
    // rejects extra folders, encoded traversal, controls and wrong sources.
    return picture_libretro_url(candidate.source, system, name) == candidate.url;
}

namespace {
bool add_candidate(std::vector<PictureCandidate>& candidates, PictureCandidate candidate) {
    if (!picture_candidate_valid(candidate)) return false;
    const auto duplicate = std::find_if(candidates.begin(), candidates.end(), [&](const PictureCandidate& item) {
        return item.url == candidate.url;
    });
    if (duplicate != candidates.end()) return true;
    const bool gametdb = candidate.source == PictureSource::GameTDB || candidate.source == PictureSource::GameTDBSecond;
    const std::size_t per_source = gametdb ? kPictureGameTDBPerSet : kPictureCandidatesPerSource;
    if (candidates.size() >= kPictureCandidatesMax || source_count(candidates, candidate.source) >= per_source) return true;
    return try_push_back(candidates, std::move(candidate));
}
}  // namespace

bool picture_add_libretro(std::vector<PictureCandidate>& candidates, PictureSource source,
                          const std::string& system, const std::string& game,
                          const std::vector<std::string>& names) {
    if (candidates.size() > kPictureCandidatesMax || names.size() > kEmulatorIndexNamesMax ||
        picture_libretro_url(source, system).empty()) return false;
    if (source_count(candidates, source) >= kPictureCandidatesPerSource || candidates.size() == kPictureCandidatesMax) return true;
    std::vector<std::string> remaining;
    if (!try_reserve(remaining, names.size())) return false;
    for (const auto& name : names) {
        if (name.size() > kEmulatorArtNameMax) return false;
        remaining.push_back(name);
    }
    // Prefer the game's exact filename, then keep the automatic lookup's
    // sequel and region rules. Identical downloads are removed later.
    for (std::size_t i = 0; i < kPictureCandidatesPerSource; ++i) {
        const std::string exact = game + ".png";
        const bool exact_present = std::find(remaining.begin(), remaining.end(), exact) != remaining.end();
        const std::string name = exact_present ? exact : match_emulator_cover(game, remaining);
        if (name.empty()) break;
        if (!add_candidate(candidates, {picture_libretro_url(source, system, name), source})) return false;
        remaining.erase(std::remove(remaining.begin(), remaining.end(), name), remaining.end());
    }
    return true;
}

bool picture_add_steamgrid(std::vector<PictureCandidate>& candidates, const std::vector<std::string>& urls) {
    if (candidates.size() > kPictureCandidatesMax || urls.size() > 256) return false;
    std::size_t added = 0;
    for (const auto& url : urls) {
        if (!steamgrid_image_url(url)) return false;
        const auto before = candidates.size();
        if (!add_candidate(candidates, {url, PictureSource::SteamGrid})) return false;
        if (candidates.size() != before && ++added == kPictureCandidatesPerSource) break;
    }
    return true;
}

bool picture_add_gametdb(std::vector<PictureCandidate>& candidates, const std::string& game_id,
                         const std::string& region, bool second) {
    if (candidates.size() > kPictureCandidatesMax || !gametdb_id(game_id) || !gametdb_region(region)) return false;
    const PictureSource source = second ? PictureSource::GameTDBSecond : PictureSource::GameTDB;
    return add_candidate(candidates, {cover_url(region, game_id, second), source});
}

std::size_t picture_record_thumbnail(std::vector<PictureCandidate>& candidates, std::size_t index,
                                     const Sha256Digest& digest) {
    if (candidates.size() > kPictureCandidatesMax || index >= candidates.size()) return kPictureCandidatesMax;
    auto& candidate = candidates[index];
    candidate.thumbnail_hash = digest;
    candidate.thumbnail_hashed = true;
    for (std::size_t other = 0; other < candidates.size(); ++other) {
        const auto& previous = candidates[other];
        if (other == index || previous.source != candidate.source || !previous.thumbnail_hashed ||
            previous.thumbnail_hash != digest) continue;
        const std::size_t duplicate = std::max(index, other);
        candidates.erase(candidates.begin() + duplicate);
        return duplicate;
    }
    return kPictureCandidatesMax;
}

std::size_t picture_page_count(std::size_t candidates) {
    if (candidates > kPictureCandidatesMax) return 0;
    return std::max<std::size_t>(1, (candidates + kPicturePageSize - 1) / kPicturePageSize);
}

bool picture_save_path_valid(const std::string& path) {
    constexpr const char* prefix = "sd:/riftwii/";
    constexpr std::size_t start = 12;
    if (path.size() > 1024 || path.compare(0, start, prefix) != 0 || path.size() < start + 5 ||
        path.compare(path.size() - 4, 4, ".png") != 0) return false;
    std::size_t at = start;
    unsigned components = 0;
    while (at < path.size()) {
        const auto end = path.find('/', at);
        const auto length = end == std::string::npos ? std::string::npos : end - at;
        const std::string component = path.substr(at, length);
        if (++components > 8 || !emulator_art_component(component)) return false;
        if (end == std::string::npos) return true;
        at = end + 1;
    }
    return false;
}

std::string picture_pick_marker(const std::vector<std::uint8_t>& png) {
    if (png.empty() || png.size() > kEmulatorCoverMax) return "";
    const auto digest = sha256(png.data(), png.size());
    static constexpr char hex[] = "0123456789abcdef";
    std::string marker = "RIFTWII-PICTURE-1\n";
    for (const auto byte : digest) {
        marker += hex[byte >> 4];
        marker += hex[byte & 15];
    }
    return marker + "\n";
}

bool picture_pick_marker_matches(const std::vector<std::uint8_t>& png, const std::string& marker) {
    return !marker.empty() && marker == picture_pick_marker(png);
}

}  // namespace riftwii
