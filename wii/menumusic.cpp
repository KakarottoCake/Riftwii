// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "menumusic.hpp"

#include <gccore.h>

#include <cstdio>

#include "loadersettings.hpp"
#include "log.hpp"
#include "oggplayer.h"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

constexpr long kMaxMusicBytes = 6 * 1024 * 1024;
constexpr int kVolume = 110;  // of 255: under the clicks, not over them

const char* const kMusicFiles[] = {"sd:/riftwii/music.ogg", "sd:/apps/riftwii/music.ogg"};

u8* g_music = nullptr;
long g_bytes = 0;
bool g_tried = false;
bool g_playing = false;

// Reads the first music file there is into MEM2 (not the MEM1 heap,
// which packs and scans need).
void Load() {
    if (g_tried) return;
    g_tried = true;
    for (const char* path : kMusicFiles) {
        FILE* f = std::fopen(path, "rb");
        if (!f) continue;
        std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size <= 0 || size > kMaxMusicBytes) {
            logf("Menu music: %s is %ld bytes; at most %ld are played\n", path, size, kMaxMusicBytes);
            std::fclose(f);
            continue;
        }
        u8* data = skin::Mem2Alloc(static_cast<std::size_t>(size));
        const bool read = data && std::fread(data, 1, static_cast<std::size_t>(size), f) == static_cast<std::size_t>(size);
        std::fclose(f);
        if (!read) {
            logf("Menu music: cannot read %s%s\n", path, data ? "" : " (MEM2 is full)");
            return;  // MEM2 taken is not given back; one try is enough
        }
        g_music = data;
        g_bytes = size;
        logf("Menu music: %s (%ld bytes)\n", path, size);
        return;
    }
}

}  // namespace

void MenuMusicStart() {
    if (g_playing || Settings().menu_music == "off") return;
    Load();
    if (!g_music) return;
    if (PlayOgg(g_music, g_bytes, 0, OGG_INFINITE_TIME) < 0) {
        logf("Menu music: the player refused the file\n");
        return;
    }
    SetVolumeOgg(kVolume);
    g_playing = true;
}

void MenuMusicStop() {
    if (!g_playing) return;
    StopOgg();
    g_playing = false;
}

bool MenuMusicFound() {
    Load();
    return g_music != nullptr;
}

}  // namespace riftwii::wii
