// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "discart.hpp"

#include <malloc.h>
#include <ogc/cache.h>
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "netsock.hpp"
#include "online.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/coverart.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kDir = "sd:/riftwii/discs";
constexpr std::time_t kRetryAfter = 7 * 24 * 60 * 60;
// GameTDB's disc pictures are some 30 to 60 KB; a larger answer is not one.
constexpr std::size_t kMaxPng = 512u << 10;

std::string StorePath(const std::string& id) { return std::string(kDir) + "/" + id + ".png"; }
std::string MissPath(const std::string& id) { return std::string(kDir) + "/" + id + ".none"; }

// A game's six characters, or a channel's four.
bool ValidId(const std::string& id) {
    if (id.size() != 6 && id.size() != 4) return false;
    for (char c : id)
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

bool WriteAll(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    const std::string temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    ok = std::fclose(f) == 0 && ok;
    // The old file goes only once the new one is whole (FAT's rename does
    // not replace a file).
    if (!ok) {
        std::remove(temp.c_str());
        return false;
    }
    std::remove(path.c_str());
    if (std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

CoverFetch FetchNow(const std::string& game_id, std::string& error) {
    if (!ValidId(game_id)) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kDir, 0777);
    for (const std::string& art_id : gametdb_art_ids(game_id)) {
        for (const std::string& region : cover_regions(game_id, MenuLanguage())) {
            std::vector<std::uint8_t> png;
            std::string why;
            if (!HttpGet(disc_art_url(region, art_id), png, why, kMaxPng, 15000)) {
                // A refusal means none in this region (the covers' rule,
                // riftwii/coverart.hpp); anything else is no answer at all.
                if (cover_answer_means_missing(http_answered_status(why))) continue;
                error = why;
                return CoverFetch::NoAnswer;
            }
            // Kept as GameTDB sent it, once it is known to be a picture the
            // page can show.
            std::vector<std::uint8_t> rgba;
            int w = 0, h = 0;
            if (!DecodePngRgba(png, rgba, w, h, why)) {
                if (took_out_of_memory()) return CoverFetch::NotFound;
                logf("Disc art: GameTDB's %s disc of %s: %s\n", region.c_str(), game_id.c_str(), why.c_str());
                continue;
            }
            if (w < 8 || h < 8 || w > kDiscArtMaxSide || h > kDiscArtMaxSide) {
                logf("Disc art: GameTDB's %s disc of %s is an unusable %dx%d\n", region.c_str(), game_id.c_str(), w, h);
                continue;
            }
            if (!WriteAll(StorePath(game_id), png)) {
                error = "cannot write " + StorePath(game_id) + " (card full?)";
                return CoverFetch::Failed;
            }
            std::remove(MissPath(game_id).c_str());
            logf("Disc art: %s stored (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
            return CoverFetch::Stored;
        }
    }
    WriteAll(MissPath(game_id), {});
    logf("Disc art: GameTDB has no disc picture of %s\n", game_id.c_str());
    return CoverFetch::NotFound;
}

struct Background {
    std::string id;
    CoverFetch got = CoverFetch::Failed;
    std::string error;
};
Background g_background;

void RunBackgroundFetch() {
    OutOfMemoryScope out_of_memory;
    g_background.got = FetchNow(g_background.id, g_background.error);
    if (g_background.got != CoverFetch::Stored && out_of_memory.failed()) {
        g_background.got = CoverFetch::NotFound;
        g_background.error = "out of memory";
        logf("Disc art: no room to read the disc picture of %s\n", g_background.id.c_str());
    }
    mem::WatchHeap(("after the disc art download of " + g_background.id).c_str());
}

// A stored picture that cannot be shown (a write cut short, a damaged
// card) goes, so the page downloads it again.
bool Unusable(const std::string& game_id, const std::string& why) {
    logf("Disc art: %s on the card cannot be used (%s); it is downloaded again\n", game_id.c_str(), why.c_str());
    std::remove(StorePath(game_id).c_str());
    return false;
}

}  // namespace

DiscArt::~DiscArt() { free(data); }

bool DiscArtWanted(const std::string& game_id) {
    if (!ValidId(game_id)) return false;
    struct stat st;
    if (stat(StorePath(game_id).c_str(), &st) == 0) return false;
    if (stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter) return false;
    return true;
}

bool StartDiscArtFetch(const std::string& game_id) {
    if (!g_background.id.empty() || NetBackgroundBusy()) return false;
    g_background = Background{};
    g_background.id = game_id;
    if (NetRunInBackground(RunBackgroundFetch)) return true;
    g_background.id.clear();
    return false;
}

const std::string& DiscArtFetchGame() { return g_background.id; }

bool TakeDiscArtFetch(std::string& game_id, CoverFetch& got, std::string& error) {
    if (g_background.id.empty() || NetBackgroundBusy()) return false;
    NetWaitForBackground();
    game_id = g_background.id;
    got = g_background.got;
    error = g_background.error;
    g_background.id.clear();
    return true;
}

bool LoadDiscArt(const std::string& game_id, DiscArt& out) {
    if (!ValidId(game_id)) return false;
    FILE* f = std::fopen(StorePath(game_id).c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || static_cast<std::size_t>(size) > kMaxPng) {
        std::fclose(f);
        return Unusable(game_id, "its size is wrong");
    }
    std::vector<std::uint8_t> png(static_cast<std::size_t>(size));
    const bool read = std::fread(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    if (!read) return Unusable(game_id, "it could not be read");
    std::vector<std::uint8_t> rgba;
    int w = 0, h = 0;
    std::string why;
    if (!DecodePngRgba(png, rgba, w, h, why)) {
        // No room is not a damaged file: it stays for the next page.
        if (took_out_of_memory()) return false;
        return Unusable(game_id, why);
    }
    int tw = 0, th = 0;
    const std::vector<std::uint8_t> texture = disc_art_texture(rgba.data(), w, h, tw, th);
    if (texture.empty()) return Unusable(game_id, "it is " + std::to_string(w) + "x" + std::to_string(h));
    u8* data = static_cast<u8*>(memalign(32, texture.size()));
    if (!data) return false;
    std::memcpy(data, texture.data(), texture.size());
    DCFlushRange(data, texture.size());
    free(out.data);
    out.data = data;
    out.w = tw;
    out.h = th;
    return true;
}

}  // namespace riftwii::wii
