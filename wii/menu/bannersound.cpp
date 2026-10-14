// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bannersound.hpp"

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>

#include <cstring>
#include <string>

#include "log.hpp"
#include "oggplayer.h"
#include "riftwii/bnrsound.hpp"

namespace riftwii::wii {
namespace {

s32 g_voice = -1;
u8* g_buf = nullptr;
std::size_t g_bytes = 0, g_loop_at = 0;
unsigned g_rate = 0;
bool g_loop = false, g_looping = false, g_music_paused = false;

}  // namespace

bool BannerSoundStart(const std::vector<std::uint8_t>& sound_bin) {
    BannerSoundStop(false);
    // A banner with no sound of its own: the menu's music plays on.
    const auto silent = [] {
        if (g_music_paused) PauseOgg(0);
        g_music_paused = false;
        return false;
    };
    if (sound_bin.empty()) return silent();
    std::string error;
    {
        BannerSound s;
        if (!decode_banner_sound(sound_bin.data(), sound_bin.size(), s, error)) {
            logf("Banner sound: %s\n", error.c_str());
            return silent();
        }
        // The DSP reads it from memory: 32-byte aligned, a whole number
        // of 32-byte blocks, flushed from the cache.
        g_bytes = (s.pcm.size() * 2 + 31) & ~std::size_t(31);
        g_buf = static_cast<u8*>(memalign(32, g_bytes));
        if (!g_buf) {
            logf("Banner sound: no memory for %u bytes\n", static_cast<unsigned>(g_bytes));
            return silent();
        }
        std::memset(g_buf, 0, g_bytes);
        std::memcpy(g_buf, s.pcm.data(), s.pcm.size() * 2);
        g_rate = s.rate;
        g_loop = s.loop;
        g_loop_at = (s.loop_start * 4) & ~std::size_t(31);
        if (g_loop_at >= g_bytes) g_loop = false;
    }
    DCFlushRange(g_buf, g_bytes);
    g_voice = ASND_GetFirstUnusedVoice();
    if (g_voice < 0) {
        logf("Banner sound: no free voice\n");
        free(g_buf);
        g_buf = nullptr;
        return silent();
    }
    g_looping = false;
    // The menu's music is paused only once the voice is really playing.
    if (ASND_SetVoice(g_voice, VOICE_STEREO_16BIT, static_cast<s32>(g_rate), 0, g_buf, static_cast<s32>(g_bytes), 255,
                      255, nullptr) != SND_OK) {
        logf("Banner sound: the mixer refused %u Hz\n", g_rate);
        free(g_buf);
        g_buf = nullptr;
        g_voice = -1;
        return false;
    }
    PauseOgg(1);
    g_music_paused = true;
    logf("Banner sound: %u Hz, %.1f s%s\n", g_rate, g_bytes / 4.0 / g_rate, g_loop ? ", looping" : "");
    return true;
}

void BannerSoundUpdate() {
    if (g_voice < 0 || !g_loop || g_looping) return;
    if (ASND_StatusVoice(g_voice) != SND_UNUSED) return;
    ASND_SetInfiniteVoice(g_voice, VOICE_STEREO_16BIT, static_cast<s32>(g_rate), 0, g_buf + g_loop_at,
                          static_cast<s32>(g_bytes - g_loop_at), 255, 255);
    g_looping = true;
}

void BannerSoundStop(bool resume_music) {
    if (g_voice >= 0) ASND_StopVoice(g_voice);
    g_voice = -1;
    if (g_buf) free(g_buf);
    g_buf = nullptr;
    g_bytes = 0;
    g_loop = g_looping = false;
    if (!resume_music) return;
    if (g_music_paused) PauseOgg(0);
    g_music_paused = false;
}

}  // namespace riftwii::wii
