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

}  // namespace

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
    std::vector<std::uint8_t> png, rgba;
    int w = 0, h = 0;
    std::string error;
    if (!read_picture(path, png)) {
        logf("Mods: %s can't be read (a PNG of 2 MB at most)\n", path.c_str());
        g_none.insert(key);
        return nullptr;
    }
    if (!DecodePngRgba(png, rgba, w, h, error)) {
        logf("Mods: %s: %s\n", path.c_str(), error.c_str());
        g_none.insert(key);
        return nullptr;
    }
    std::vector<std::uint8_t>().swap(png);
    const std::vector<std::uint8_t> box = fit_art(rgba.data(), w, h, kModPictureW, kModPictureH);
    std::vector<std::uint8_t>().swap(rgba);
    if (!victim->data) victim->data = skin::Mem2Alloc(kSlotBytes);
    if (!victim->data || !to_gx_rgba8(box.data(), kModPictureW, kModPictureH, victim->data)) {
        g_none.insert(key);
        return nullptr;
    }
    DCFlushRange(victim->data, kSlotBytes);
    victim->path = path;
    victim->used = ++g_clock;
    logf("Mods: picture %s (%dx%d)\n", path.c_str(), w, h);
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
