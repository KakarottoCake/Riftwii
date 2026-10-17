// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "modpicture.hpp"

#include <gccore.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <set>
#include <vector>

#include "covers.hpp"
#include "log.hpp"
#include "riftwii/canvas.hpp"
#include "riftwii/modart.hpp"
#include "skin.hpp"
#include "usbcatalog.hpp"
#include "vsdimage.hpp"

namespace riftwii::wii {
namespace {

constexpr long kMaxPng = 2 * 1024 * 1024;  // a cover from GameTDB is a few hundred KB at most
constexpr int kSlots = 6;
constexpr std::size_t kSlotBytes = static_cast<std::size_t>(kModPictureW) * kModPictureH * 4;

struct Slot {
    std::string path;
    u8* data = nullptr;
    unsigned used = 0;  // when it was last asked for
};
Slot g_slots[kSlots];
unsigned g_clock = 0;
std::set<std::string> g_none;  // mods with no picture (or one that can't be read), by key

std::string image_location(const LaunchPackage& p) {
    if (p.gct_path.compare(0, 5, "vsd:/") != 0) return "";
    return VsdImageLocation(VsdImageOfKey(p.file));
}

std::vector<std::string> candidates(const LaunchPackage& p) {
    return mod_art_paths(p.code_build() ? std::string() : p.path, p.gct_path, image_location(p));
}

// The whole file, from the SD card or the menu's view of the USB drive.
bool read_picture(const std::string& path, std::vector<std::uint8_t>& out) {
    if (path.compare(0, 5, "usb:/") == 0) {
        std::uint64_t size = 0;
        std::uint8_t probe;
        if (!usb_volume_ready() || !read_usb_range(path, 0, &probe, 0, size) || size == 0 ||
            size > static_cast<std::uint64_t>(kMaxPng))
            return false;
        out.resize(static_cast<std::size_t>(size));
        return read_usb_range(path, 0, out.data(), out.size(), size);
    }
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || S_ISDIR(st.st_mode) || st.st_size <= 0 || st.st_size > kMaxPng) return false;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(static_cast<std::size_t>(st.st_size));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

bool exists(const std::string& path) {
    if (path.compare(0, 5, "usb:/") == 0) {
        std::uint64_t size = 0;
        std::uint8_t probe;
        return usb_volume_ready() && read_usb_range(path, 0, &probe, 0, size);
    }
    struct stat st;
    return stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

// A picture file read and fitted into a `w` x `h` RGBA8 texture at `out`
// (`what` names it in the log). False when it can't be read.
bool load_fitted(const std::string& path, int bw, int bh, u8* out, const char* what) {
    std::vector<std::uint8_t> png, rgba;
    int w = 0, h = 0;
    std::string error;
    if (!read_picture(path, png)) {
        logf("%s: %s can't be read (a PNG of 2 MB at most)\n", what, path.c_str());
        return false;
    }
    if (!DecodePngRgba(png, rgba, w, h, error)) {
        logf("%s: %s: %s\n", what, path.c_str(), error.c_str());
        return false;
    }
    std::vector<std::uint8_t>().swap(png);
    const std::vector<std::uint8_t> box = fit_art(rgba.data(), w, h, bw, bh);
    std::vector<std::uint8_t>().swap(rgba);
    if (!to_gx_rgba8(box.data(), bw, bh, out)) return false;
    DCFlushRange(out, static_cast<std::size_t>(bw) * bh * 4);
    logf("%s: picture %s (%dx%d)\n", what, path.c_str(), w, h);
    return true;
}

// The profiles' pictures, by profile key: a page of tiles' worth.
constexpr int kProfileSlots = 12;
constexpr std::size_t kProfileSlotBytes = static_cast<std::size_t>(kProfilePicture) * kProfilePicture * 4;
Slot g_profileSlots[kProfileSlots];
std::set<std::string> g_profileNone;

}  // namespace

std::string ModPicturePath(const LaunchPackage& p) {
    for (const std::string& c : candidates(p))
        if (exists(c)) return c;
    return "";
}

const u8* ProfilePicture(const std::string& key, const std::string& art) {
    if (key.empty() || g_profileNone.count(key)) return nullptr;
    Slot* victim = &g_profileSlots[0];
    for (Slot& s : g_profileSlots) {
        if (s.data && s.path == key) {
            s.used = ++g_clock;
            return s.data;
        }
        if (s.used < victim->used) victim = &s;
    }
    // The player's own picture first, then the pack's.
    std::string path = std::string(kProfilePictureDir) + "/" + key + ".png";
    if (!exists(path)) path = art;
    if (path.empty() || !exists(path)) {
        g_profileNone.insert(key);
        return nullptr;
    }
    if (!victim->data) victim->data = skin::Mem2Alloc(kProfileSlotBytes);
    victim->path.clear();
    if (!victim->data || !load_fitted(path, kProfilePicture, kProfilePicture, victim->data, "Home tile")) {
        g_profileNone.insert(key);
        return nullptr;
    }
    victim->path = key;
    victim->used = ++g_clock;
    return victim->data;
}

void ForgetProfilePictures() {
    g_profileNone.clear();
    for (Slot& s : g_profileSlots) {
        s.path.clear();
        s.used = 0;
    }
}

const u8* ModPicture(const LaunchPackage& p) {
    const std::string key = p.file + "\n" + p.path + "\n" + p.gct_path;
    if (g_none.count(key)) return nullptr;
    std::string path;
    for (const std::string& c : candidates(p))
        if (exists(c)) {
            path = c;
            break;
        }
    if (path.empty()) {
        g_none.insert(key);
        return nullptr;
    }
    Slot* victim = &g_slots[0];
    for (Slot& s : g_slots) {
        if (s.data && s.path == path) {
            s.used = ++g_clock;
            return s.data;
        }
        if (s.used < victim->used) victim = &s;
    }
    if (!victim->data) victim->data = skin::Mem2Alloc(kSlotBytes);
    victim->path.clear();
    if (!victim->data || !load_fitted(path, kModPictureW, kModPictureH, victim->data, "Mods")) {
        g_none.insert(key);
        return nullptr;
    }
    victim->path = path;
    victim->used = ++g_clock;
    return victim->data;
}

std::string ModPictureName(const LaunchPackage& p) {
    const std::vector<std::string> c = candidates(p);
    if (c.empty()) return "";
    const std::string& path = c.front();
    const std::size_t slash = path.rfind('/');
    std::string name = path.substr(slash + 1);
    // A code build's cover.png: say which folder it goes in.
    if (p.code_build() && image_location(p).empty() && slash != std::string::npos) {
        const std::size_t up = path.rfind('/', slash - 1);
        if (up != std::string::npos) name = path.substr(up + 1);
    }
    return name;
}

void ForgetModPictures() { g_none.clear(); }

}  // namespace riftwii::wii
