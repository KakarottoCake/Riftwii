// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "themesounds.hpp"

#include <gccore.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "filelist.h"
#include "libwiigui/gui.h"
#include "log.hpp"
#include "menutheme.hpp"
#include "riftwii/bnrsound.hpp"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

// A sound effect is short: the file at most 1 MB, the sound at most 5 s.
constexpr long kMaxFileBytes = 1024 * 1024;
constexpr unsigned kMaxSeconds = 5;

struct Slot {
    const char* file;  // in the theme's folder
    const u8* builtin;  // RiftWii's own, which GuiSound is given
    const u8* data = nullptr;  // the theme's, stereo 16-bit in MEM2
    s32 bytes = 0;
    s32 rate = 0;
};

Slot g_slots[] = {
    {"hover.wav", button_over_pcm},
    {"click.wav", button_click_pcm},
    {"home_open.wav", home_open_pcm},
    {"home_close.wav", home_close_pcm},
};

void Substitute(const u8*& sound, s32& length, s32& rate) {
    for (const Slot& s : g_slots) {
        if (sound == s.builtin && s.data) {
            sound = s.data;
            length = s.bytes;
            rate = s.rate;
            return;
        }
    }
}

bool Load(Slot& slot, const std::string& path, std::string& why) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;  // not there: RiftWii's, nothing to say
    std::fseek(f, 0, SEEK_END);
    const long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > kMaxFileBytes) {
        std::fclose(f);
        why = std::to_string(size) + " bytes, at most " + std::to_string(kMaxFileBytes);
        return false;
    }
    std::vector<std::uint8_t> file(static_cast<std::size_t>(size));
    const bool read = std::fread(file.data(), 1, file.size(), f) == file.size();
    std::fclose(f);
    if (!read) {
        why = "unreadable";
        return false;
    }
    BannerSound s;
    if (!decode_banner_sound(file.data(), file.size(), s, why, 48000 * kMaxSeconds)) return false;
    if (s.frames() > s.rate * kMaxSeconds) s.pcm.resize(std::size_t(s.rate) * kMaxSeconds * 2);
    // The DSP reads it from memory: 32-byte aligned, a whole number of
    // 32-byte blocks, flushed from the cache.
    const std::size_t bytes = (s.pcm.size() * 2 + 31) & ~std::size_t(31);
    u8* buf = skin::Mem2Alloc(bytes);
    if (!buf) {
        why = "no memory for " + std::to_string(bytes) + " bytes";
        return false;
    }
    std::memset(buf, 0, bytes);
    std::memcpy(buf, s.pcm.data(), s.pcm.size() * 2);
    DCFlushRange(buf, bytes);
    slot.data = buf;
    slot.bytes = static_cast<s32>(bytes);
    slot.rate = static_cast<s32>(s.rate);
    return true;
}

}  // namespace

void LoadThemeSounds() {
    static bool loaded = false;
    if (loaded) return;
    loaded = true;
    const std::string& dir = MenuThemeDir();
    if (dir.empty()) return;
    unsigned replaced = 0;
    for (Slot& slot : g_slots) {
        std::string why;
        if (Load(slot, dir + slot.file, why)) {
            ++replaced;
            logf("Theme: %s, %u ms at %u Hz\n", slot.file,
                 static_cast<unsigned>(slot.bytes / 4 * 1000ull / static_cast<unsigned>(slot.rate)),
                 static_cast<unsigned>(slot.rate));
        } else if (!why.empty()) {
            logf("Theme: %s skipped (%s); RiftWii's own sound plays\n", slot.file, why.c_str());
        }
    }
    if (replaced) GuiSound::substitute = Substitute;
}

}  // namespace riftwii::wii
