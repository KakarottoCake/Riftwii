// SPDX-License-Identifier: GPL-3.0-or-later
#include "covers.hpp"

#include <ogc/cache.h>
#include <png.h>
#include <sys/stat.h>

#include <cstdio>
#include <ctime>
#include <set>
#include <vector>

#include "loadersettings.hpp"
#include "log.hpp"
#include "online.hpp"
#include "riftwii/coverart.hpp"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

constexpr const char* kCoverDir = "sd:/riftwii/covers";
constexpr std::time_t kRetryAfter = 7 * 24 * 60 * 60;
constexpr int kPoolSize = 13;  // a page of tiles and the game page

std::string CoverPath(const std::string& id) { return std::string(kCoverDir) + "/" + id + ".rwc"; }
// An empty file: GameTDB had no cover when it was written.
std::string MissPath(const std::string& id) { return std::string(kCoverDir) + "/" + id + ".none"; }

bool ValidId(const std::string& id) {
    if (id.size() != 6) return false;
    for (char c : id) {
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

bool WriteAll(const std::string& path, const std::vector<std::uint8_t>& bytes) {
    const std::string temp = path + ".part";
    FILE* f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    const bool ok = bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    std::remove(path.c_str());
    if (!ok || std::rename(temp.c_str(), path.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

// PNG to RGBA rows, with libpng's simplified reader.
bool DecodePng(const std::vector<std::uint8_t>& png, std::vector<std::uint8_t>& rgba, int& w, int& h) {
    png_image image = {};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, png.data(), png.size())) return false;
    image.format = PNG_FORMAT_RGBA;
    if (image.width == 0 || image.height == 0 || image.width > 1024 || image.height > 1024) {
        png_image_free(&image);
        return false;
    }
    rgba.assign(PNG_IMAGE_SIZE(image), 0);
    if (!png_image_finish_read(&image, nullptr, rgba.data(), 0, nullptr)) {
        png_image_free(&image);
        return false;
    }
    w = static_cast<int>(image.width);
    h = static_cast<int>(image.height);
    return true;
}

struct Slot {
    std::string id;
    u8* data = nullptr;
    unsigned used = 0;
};
Slot g_pool[kPoolSize];
unsigned g_clock = 0;
std::set<std::string> g_absent;  // asked for and not on the card

}  // namespace

bool CoverWanted(const std::string& game_id) {
    if (!ValidId(game_id)) return false;
    struct stat st;
    if (stat(CoverPath(game_id).c_str(), &st) == 0) return false;
    if (stat(MissPath(game_id).c_str(), &st) == 0 && std::time(nullptr) - st.st_mtime < kRetryAfter) return false;
    return true;
}

bool CoverStored(const std::string& game_id) {
    struct stat st;
    return ValidId(game_id) && stat(CoverPath(game_id).c_str(), &st) == 0;
}

CoverFetch FetchCover(const std::string& game_id, std::string& error) {
    if (!ValidId(game_id)) return CoverFetch::NotFound;
    mkdir("sd:/riftwii", 0777);
    mkdir(kCoverDir, 0777);
    for (const std::string& region : cover_regions(game_id, MenuLanguage())) {
        std::vector<std::uint8_t> png;
        std::string why;
        if (!HttpGet(cover_url(region, game_id), png, why, 512u << 10, 8000)) {
            static const std::string kMissing = " answered 404";  // HttpGet's words for a 404
            if (why.size() >= kMissing.size() && why.compare(why.size() - kMissing.size(), kMissing.size(), kMissing) == 0) continue;
            error = why;
            return CoverFetch::Failed;
        }
        std::vector<std::uint8_t> rgba;
        int w = 0, h = 0;
        if (!DecodePng(png, rgba, w, h)) {
            logf("Covers: GameTDB's %s cover of %s is not a PNG we can read\n", region.c_str(), game_id.c_str());
            continue;
        }
        const std::vector<std::uint8_t> file = make_cover_file(rgba.data(), w, h);
        if (file.empty()) continue;
        if (!WriteAll(CoverPath(game_id), file)) {
            error = "cannot write " + CoverPath(game_id) + " (card full?)";
            return CoverFetch::Failed;
        }
        std::remove(MissPath(game_id).c_str());
        logf("Covers: %s (%s, %dx%d)\n", game_id.c_str(), region.c_str(), w, h);
        return CoverFetch::Stored;
    }
    WriteAll(MissPath(game_id), {});
    logf("Covers: GameTDB has none for %s\n", game_id.c_str());
    return CoverFetch::NotFound;
}

const u8* CoverTexture(const std::string& game_id) {
    if (!ValidId(game_id) || g_absent.count(game_id)) return nullptr;
    ++g_clock;
    for (Slot& s : g_pool) {
        if (s.data && s.id == game_id) {
            s.used = g_clock;
            return s.data;
        }
    }
    FILE* f = std::fopen(CoverPath(game_id).c_str(), "rb");
    if (!f) {
        g_absent.insert(game_id);
        return nullptr;
    }
    // The least recently drawn slot, allocated the first time.
    Slot* slot = &g_pool[0];
    for (Slot& s : g_pool) {
        if (!s.data || s.used < slot->used) slot = &s;
        if (!s.data) break;
    }
    if (!slot->data) slot->data = skin::Mem2Alloc(kCoverPixelBytes);
    std::uint8_t header[kCoverHeaderSize];
    const bool ok = slot->data && std::fread(header, 1, sizeof(header), f) == sizeof(header) && cover_header_valid(header) &&
                    std::fread(slot->data, 1, kCoverPixelBytes, f) == kCoverPixelBytes;
    std::fclose(f);
    if (!ok) {
        slot->id.clear();
        g_absent.insert(game_id);
        return nullptr;
    }
    DCFlushRange(slot->data, kCoverPixelBytes);
    slot->id = game_id;
    slot->used = g_clock;
    return slot->data;
}

void ForgetCover(const std::string& game_id) {
    g_absent.erase(game_id);
    for (Slot& s : g_pool) {
        if (s.id == game_id) s.id.clear();
    }
}

}  // namespace riftwii::wii
