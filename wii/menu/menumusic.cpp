// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "menumusic.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

#include "audio.h"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menutheme.hpp"
#include "oggplayer.h"
#include "skin.hpp"

namespace riftwii::wii {
namespace {

constexpr long kMaxMusicBytes = 6 * 1024 * 1024;
constexpr int kVolume = 110;  // of 255: under the clicks, not over them
// The fade at the end of a launch: half a second, in 25 steps of 20 ms,
// each too small a drop in volume to be heard as a step.
constexpr int kFadeSteps = 25;
constexpr int kFadeStepUs = 20000;
// Back after a cIOS reload: a fifth of a second, so the track does not
// cut back in.
constexpr int kFadeInSteps = 10;

const char* const kMusicFiles[] = {"sd:/riftwii/music.ogg", "sd:/apps/riftwii/music.ogg"};

u8* g_music = nullptr;
long g_bytes = 0;
bool g_tried = false;
bool g_playing = false;
bool g_kept_for_launch = false;
s32 g_resume_at_ms = -1;  // where the track was when a cIOS reload paused it

// Reads the first music file there is into MEM2 (not the MEM1 heap,
// which packs and scans need).
void Load() {
    if (g_tried) return;
    g_tried = true;
    // The theme's music.ogg first (docs/THEMES.md), then the card's, then RiftWii's.
    const std::string theme_music = MenuThemeMusic();
    std::vector<const char*> paths;
    if (!theme_music.empty()) paths.push_back(theme_music.c_str());
    for (const char* path : kMusicFiles) paths.push_back(path);
    for (const char* path : paths) {
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

// The half-second fade both endings of the music share, so they cannot drift
// apart: the volume steps down to silence, the decoder keeps running.
void FadeVolumeToZero() {
    for (int step = kFadeSteps - 1; step >= 0; --step) {
        SetVolumeOgg(kVolume * step / kFadeSteps);
        usleep(kFadeStepUs);
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
    g_kept_for_launch = false;
    g_resume_at_ms = -1;
    if (!g_playing) return;
    StopOgg();
    g_playing = false;
}

void MenuMusicFadeOut() {
    if (!g_playing) return;
    FadeVolumeToZero();
    MenuMusicStop();
}

bool MenuMusicFound() {
    Load();
    return g_music != nullptr;
}

bool MenuMusicKeepForLaunch() {
    if (!g_playing) return false;
    // "Fade out": nothing is kept, so nothing is paused or resumed around the
    // cIOS reload either (g_resume_at_ms stays -1): the music stays off for the
    // whole launch. "Keep playing" fades out and back in around that reload
    // (LaunchMusicPauseForReload / LaunchMusicResumeAfterReload), by design.
    if (Settings().launch_music == "off") {
        logf("Menu music: fades out as the menu closes (Settings: Launch music is Fade out)\n");
        return false;
    }
    // The decoder runs on its own thread from the file in MEM2 and the
    // mixer on the audio interrupt, so neither needs the GUI thread, which
    // is halted from here on, nor the card, which the launch reads.
    g_kept_for_launch = true;
    return true;
}

namespace {

// Fades the kept music out, stops the decoder (its thread joined, Tremor's
// memory freed) and shuts the audio down, so nothing of it runs on: not
// the decoder, not the DSP's mixing, not the audio DMA.
void FadeOutAndStop(const char* why, s32 resume_at_ms) {
    const u64 start = gettime();
    FadeVolumeToZero();
    MenuMusicStop();
    ShutdownAudio();
    g_resume_at_ms = resume_at_ms;
    logf("Menu music: faded out in %u ms (%s)\n",
         static_cast<unsigned>(ticks_to_millisecs(gettime() - start)), why);
}

}  // namespace

void LaunchMusicEnd(const char* why) {
    // Music paused for a cIOS reload stays paused (reload_ios calls this
    // too): it comes back only if that reload works.
    if (!g_kept_for_launch) return;
    FadeOutAndStop(why, -1);
}

bool LaunchMusicPlaying() { return g_kept_for_launch; }

void LaunchMusicPauseForReload(const char* why) {
    if (!g_kept_for_launch) return;
    // The audio must not run across an IOS reload: on a Wii, music left
    // playing across the reload into d2x (IOS249) damaged RiftWii's heap
    // and the decoder's state (2610-452, a USB game), which Dolphin did
    // not show. Stopped here, as for the game, and started again after.
    const s32 at_ms = GetTimeOgg();
    FadeOutAndStop(why, at_ms >= 0 ? at_ms : 0);
}

void LaunchMusicResumeAfterReload() {
    if (g_resume_at_ms < 0) return;
    const s32 at_ms = g_resume_at_ms;
    g_resume_at_ms = -1;
    InitAudio();
    if (PlayOgg(g_music, g_bytes, at_ms, OGG_INFINITE_TIME) < 0) {
        ShutdownAudio();
        logf("Menu music: not resumed after the reload (the player refused the file)\n");
        return;
    }
    // PlayOgg starts at full volume; its thread hands the mixer nothing
    // before its second decode, after it has yielded back to here.
    SetVolumeOgg(0);
    g_playing = true;
    g_kept_for_launch = true;
    for (int step = 1; step <= kFadeInSteps; ++step) {
        SetVolumeOgg(kVolume * step / kFadeInSteps);
        usleep(kFadeStepUs);
    }
    logf("Menu music: back after the reload, %d.%01d s into the track\n", static_cast<int>(at_ms / 1000),
         static_cast<int>(at_ms % 1000 / 100));
}

}  // namespace riftwii::wii
