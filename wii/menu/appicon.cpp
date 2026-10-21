// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "appicon.hpp"

#include <gccore.h>
#include <sys/stat.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <set>
#include <vector>

#include "covers.hpp"
#include "log.hpp"
#include "riftwii/canvas.hpp"
#include "riftwii/modart.hpp"
#include "usbcatalog.hpp"

namespace riftwii::wii {
namespace {

constexpr int kIconW = 128;
constexpr int kIconH = 48;
constexpr int kSlots = 24;  // the Homebrew view's pages, and their neighbours
constexpr long kMaxPng = 1 << 20;

struct Slot {
    std::string folder;
    u8* data = nullptr;
    unsigned used = 0;
    bool ready = false;
};

Slot g_slots[kSlots];
unsigned g_clock = 0;
std::set<std::string> g_failed;

bool read_png(const std::string& path, std::vector<std::uint8_t>& out) {
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
    if (f == nullptr) return false;
    out.resize(static_cast<std::size_t>(st.st_size));
    const bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

}  // namespace

bool AppIcon(const std::string& folder, skin::Tex& out) {
    out = skin::Tex{};
    if (folder.empty()) return false;
    for (Slot& slot : g_slots) {
        if (slot.ready && slot.folder == folder) {
            slot.used = ++g_clock;
            out.data = slot.data;
            out.w = kIconW;
            out.h = kIconH;
            return true;
        }
    }
    return false;
}

bool AppIconWanted(const std::string& folder) {
    if (folder.empty() || g_failed.count(folder)) return false;
    for (const Slot& slot : g_slots)
        if (slot.ready && slot.folder == folder) return false;
    return true;
}

void LoadAppIcon(const std::string& folder) {
    if (!AppIconWanted(folder)) return;
    Slot* victim = &g_slots[0];
    for (Slot& slot : g_slots)
        if (slot.used < victim->used) victim = &slot;
    // Remember failures too: a missing or damaged PNG must not be read
    // and decoded on every menu turn until the next rescan.
    g_failed.insert(folder);
    std::vector<std::uint8_t> png, rgba;
    int w = 0, h = 0;
    std::string error;
    if (!read_png(folder + "/icon.png", png) || !DecodePngRgba(png, rgba, w, h, error)) {
        if (!error.empty()) logf("Homebrew: %s/icon.png: %s\n", folder.c_str(), error.c_str());
        return;
    }
    std::vector<std::uint8_t>().swap(png);
    const std::vector<std::uint8_t> box = riftwii::fit_art(rgba.data(), w, h, kIconW, kIconH);
    std::vector<std::uint8_t>().swap(rgba);
    if (!victim->data) victim->data = skin::Mem2Alloc(static_cast<std::size_t>(kIconW) * kIconH * 4);
    if (victim->data == nullptr || !riftwii::to_gx_rgba8(box.data(), kIconW, kIconH, victim->data)) {
        victim->ready = false;
        return;
    }
    DCFlushRange(victim->data, static_cast<std::size_t>(kIconW) * kIconH * 4);
    victim->folder = folder;
    victim->used = ++g_clock;
    victim->ready = true;
    g_failed.erase(folder);
}

void ForgetAppIcons() {
    for (Slot& slot : g_slots) {
        // MEM2 textures come from a bump allocator. Keep their buffers
        // for reuse; neither free() nor allocating replacements is valid.
        slot.folder.clear();
        slot.used = 0;
        slot.ready = false;
    }
    g_clock = 0;
    g_failed.clear();
}

}  // namespace riftwii::wii
