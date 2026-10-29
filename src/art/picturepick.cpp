// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/picturepick.hpp"

#include <algorithm>

#include "riftwii/checkedalloc.hpp"
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
        case PictureSource::SteamGrid: return "";
        default: return "";
    }
    return url;
}

bool picture_candidate_valid(const PictureCandidate& candidate) {
    if (candidate.url.size() > 2048) return false;
    if (candidate.source == PictureSource::SteamGrid) return steamgrid_image_url(candidate.url);
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
    if (candidates.size() >= kPictureCandidatesMax ||
        source_count(candidates, candidate.source) >= kPictureCandidatesPerSource) return true;
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
    // Keep the automatic lookup's sequel and region rules. Remove each
    // best match so equivalent regional pictures can also be offered.
    for (std::size_t i = 0; i < kPictureCandidatesPerSource; ++i) {
        const std::string name = match_emulator_cover(game, remaining);
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
