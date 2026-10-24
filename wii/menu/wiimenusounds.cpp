// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "wiimenusounds.hpp"

#include <asndlib.h>
#include <gccore.h>
#include <malloc.h>
#include <ogc/lwp_watchdog.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "boot.hpp"
#include "filelist.h"
#include "libwiigui/gui.h"
#include "loadersettings.hpp"
#include "log.hpp"
#include "memlimits.hpp"
#include "nanduse.hpp"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/nandchannel.hpp"
#include "riftwii/sysfont.hpp"
#include "riftwii/wiisound.hpp"
#include "skin.hpp"
#include "themesounds.hpp"

namespace riftwii::wii {
namespace {

constexpr std::size_t kMaxContent = 16 * 1024 * 1024;
constexpr std::size_t kMaxArchive = 4 * 1024 * 1024;
constexpr std::size_t kMaxEffects = 1536 * 1024;
constexpr std::size_t kStreamFrames = 2048;
constexpr std::size_t kStreamBytes = kStreamFrames * 4;

struct Effect {
    const char* name;
    u8* pcm = nullptr;
    s32 bytes = 0;
};
// Names, never sample indices: regions and modified menus can reorder files.
Effect g_effects[] = {
    {"WIPL_SE_BT_TARGETTING"}, {"WIPL_SE_DECIDE"}, {"WIPL_SE_CH_SELECT"},
    {"WIPL_SE_CANCEL"}, {"WIPL_SE_INFO_WINDOW"}, {"WIPL_SE_FL_PAGE_INC"},
    {"WIPL_SE_FL_PAGE_DEC"}, {"WIPL_SE_WII_START"}, {"HOMESE_HOME_BUTTON"}, {"HOMESE_RETURN_APP"},
};
static_assert(std::size(g_effects) == static_cast<std::size_t>(MenuSoundEvent::Count));

WiiSoundArchive g_archive;
bool g_archive_tried = false;
bool g_archive_ready = false;
bool g_effects_tried = false;
bool g_music_tried = false;
std::size_t g_resident_bytes = 0;
WiiSoundPlayer g_music_player;
bool g_music_ready = false;
u8* g_stream = nullptr;
lwp_t g_music_thread = LWP_THREAD_NULL;
std::atomic<bool> g_running{false};
std::atomic<bool> g_paused{false};
std::atomic<bool> g_failed{false};
std::atomic<int> g_volume{110};
alignas(32) u8 g_music_stack[32768];

using NandBuffer = std::unique_ptr<u8, decltype(&std::free)>;

// Only the NAND reader temporarily opens IOS's permission check, and closes
// it on every exit. No title contents or decoded sounds are cached on disk.
NandBuffer ReadNand(const std::string& path, std::size_t limit, std::size_t& size, std::string& why) {
    s32 fd = ISFS_Open(path.c_str(), ISFS_OPEN_READ);
    struct Permission {
        bool opened = false;
        ~Permission() {
            if (opened) close_nand_permissions("Wii Menu sounds");
        }
    } permission;
    if (fd == -102 && open_nand_permissions("Wii Menu sounds")) {
        permission.opened = true;
        fd = ISFS_Open(path.c_str(), ISFS_OPEN_READ);
    }
    NandBuffer result(nullptr, &std::free);
    if (fd < 0) {
        why = path + " could not be opened (" + std::to_string(fd) + ")";
        return result;
    }
    fstats stats ATTRIBUTE_ALIGN(32) = {};
    if (ISFS_GetFileStats(fd, &stats) < 0 || stats.file_length == 0 || stats.file_length > limit) {
        ISFS_Close(fd);
        why = path + " is empty, unreadable or exceeds the sound budget";
        return result;
    }
    size = stats.file_length;
    result.reset(static_cast<u8*>(memalign(32, (size + 31) & ~std::size_t(31))));
    const s32 got = result ? ISFS_Read(fd, result.get(), size) : -1;
    ISFS_Close(fd);
    if (got != static_cast<s32>(size)) {
        result.reset();
        why = path + " could not be read into memory";
    }
    return result;
}

bool ReadArchive(std::string& why) {
    if (NandOpen() < 0) {
        why = "the NAND could not be opened";
        return false;
    }
    struct CloseNand { ~CloseNand() { NandClose(); } } close_nand;
    std::size_t size = 0;
    NandBuffer tmd_bytes = ReadNand("/title/00000001/00000002/content/title.tmd", 64 * 1024, size, why);
    TmdContents tmd;
    if (!tmd_bytes || !parse_tmd_contents(tmd_bytes.get(), size, tmd, why)) return false;
    if (tmd.title_id != 0x0000000100000002ull || tmd.contents.size() > 64) {
        why = "the System Menu title metadata is invalid";
        return false;
    }
    std::size_t map_size = 0;
    NandBuffer map(nullptr, &std::free);
    std::size_t examined = 0;
    for (const TmdContent& content : tmd.contents) {
        if (content.index == tmd.boot_index || content.size < 32 || content.size > kMaxContent) continue;
        if (++examined > 16) {
            why = "the System Menu has too many resource contents";
            return false;
        }
        std::string path = nand_content_path(tmd.title_id, content.id);
        if (content.type & 0x8000) {
            if (!map) map = ReadNand("/shared1/content.map", 1024 * 1024, map_size, why);
            if (!map) continue;
            const std::string name = shared_content_name(map.get(), map_size, content.sha1.data());
            if (name.empty()) continue;
            path = "/shared1/" + name + ".app";
        }
        NandBuffer bytes = ReadNand(path, kMaxContent, size, why);
        if (!bytes) continue;
        std::vector<SoundResource> files;
        if (!list_sound_u8({bytes.get(), size}, files, why)) continue;
        for (const SoundResource& file : files) {
            const std::size_t slash = file.name.find_last_of('/');
            const std::string name = file.name.substr(slash == std::string::npos ? 0 : slash + 1);
            if (name != "IplSound.brsar") continue;
            WiiSoundArchive checked;
            if (file.bytes.size > kMaxArchive || !checked.load(file.bytes, why)) return false;
            u8* kept = skin::Mem2Alloc(file.bytes.size);
            if (!kept) {
                why = "there is no MEM2 for the System Menu sound archive";
                return false;
            }
            std::memcpy(kept, file.bytes.data, file.bytes.size);
            if (!g_archive.load({kept, file.bytes.size}, why)) {
                skin::Mem2Release(kept, file.bytes.size);
                return false;
            }
            g_resident_bytes += (file.bytes.size + 31) & ~std::size_t(31);
            logf("Wii Menu sounds: read %s/%s (%u bytes) into memory only\n", path.c_str(), file.name.c_str(),
                 static_cast<unsigned>(file.bytes.size));
            return true;
        }
    }
    if (why.empty()) why = "the System Menu has no readable IplSound.brsar";
    return false;
}

bool ArchiveReady() {
    if (!g_archive_tried) {
        g_archive_tried = true;
        std::string why;
        g_archive_ready = ReadArchive(why);
        if (!g_archive_ready)
            logf("Wii Menu sounds: %s; RiftWii's own sounds and music are used\n", why.c_str());
    }
    return g_archive_ready;
}

bool DecodeEffect(Effect& effect, std::size_t& total, std::string& why) {
    WiiSoundPlayer player;
    if (!player.prepare(g_archive, effect.name, false, why)) return false;
    std::vector<std::int16_t> pcm;
    std::array<std::int16_t, 2048> block{};
    while (!player.finished() && pcm.size() < WiiSoundPlayer::kRate * 5 * 2) {
        if (!player.render(block.data(), block.size() / 2)) {
            why = player.error();
            return false;
        }
        if (!try_append(pcm, block.begin(), block.end())) {
            why = "no memory to render the sound";
            return false;
        }
    }
    if (!player.finished()) {
        why = "the sound exceeds five seconds";
        return false;
    }
    // Drop the renderer's last silent block tail, keeping stereo frames whole.
    while (pcm.size() >= 2 && pcm[pcm.size() - 1] == 0 && pcm[pcm.size() - 2] == 0) pcm.resize(pcm.size() - 2);
    const std::size_t bytes = (pcm.size() * 2 + 31) & ~std::size_t(31);
    if (bytes == 0 || bytes > kMaxEffects - total) {
        why = "the sound exceeds the decoded effect budget";
        return false;
    }
    u8* data = skin::Mem2Alloc(bytes);
    if (!data) {
        why = "there is no MEM2 for the decoded sound";
        return false;
    }
    std::memset(data, 0, bytes);
    std::memcpy(data, pcm.data(), pcm.size() * 2);
    DCFlushRange(data, bytes);
    effect.pcm = data;
    effect.bytes = static_cast<s32>(bytes);
    total += bytes;
    logf("Wii Menu sounds: loaded %s (%u bytes, %u Hz)\n", effect.name,
         static_cast<unsigned>(bytes), WiiSoundPlayer::kRate);
    return true;
}

void StreamCallback(s32) {
    // Keeps the voice in SND_WAITING on an underrun. Rendering and logging
    // belong to the producer thread, never the mixer callback.
}

void* MusicThread(void*) {
    unsigned buffer = 1;
    while (g_running.load(std::memory_order_acquire)) {
        u8* data = g_stream + buffer * kStreamBytes;
        if (g_paused.load() || ASND_TestPointer(0, data) != 0) {
            usleep(2000);
            continue;
        }
        if (!g_music_player.render(reinterpret_cast<std::int16_t*>(data), kStreamFrames)) {
            logf("Wii Menu music: playback stopped because %s; RiftWii's own music is used\n", g_music_player.error().c_str());
            g_failed.store(true, std::memory_order_release);
            g_running.store(false, std::memory_order_release);
            break;
        }
        DCFlushRange(data, kStreamBytes);
        while (g_running.load() && ASND_TestVoiceBufferReady(0) != 1) usleep(2000);
        if (!g_running.load()) break;
        // ASND can report ready while SetVoice's initial update is still
        // pending. AddVoice then returns INVALID until the next mixer tick.
        // Retain this rendered block and retry; never advance the sequence
        // again for a buffer that has not been accepted.
        unsigned retries = 0;
        while (g_running.load() && ASND_AddVoice(0, data, kStreamBytes) != SND_OK) {
            if (++retries == 250) {
                logf("Wii Menu music: the mixer refused the next buffer for half a second; RiftWii's own music is used\n");
                g_failed.store(true, std::memory_order_release);
                g_running.store(false, std::memory_order_release);
                break;
            }
            usleep(2000);
        }
        buffer ^= 1;
    }
    return nullptr;
}

}  // namespace

void LoadWiiMenuSounds() {
    if (!Settings().wii_menu_sounds || g_effects_tried) return;
    g_effects_tried = true;
    if (!ArchiveReady()) return;
    std::size_t total = 0;
    for (Effect& effect : g_effects) {
        std::string why;
        if (!DecodeEffect(effect, total, why))
            logf("Wii Menu sounds: %s was not loaded because %s; RiftWii's own sound is used\n", effect.name, why.c_str());
    }
    g_resident_bytes += total;
    logf("Wii Menu sounds: memory budget has %u resident archive, effect and stream bytes (archive at most 4 MiB, effects 1536 KiB)\n",
         static_cast<unsigned>(g_resident_bytes));
    mem::LogUsage("Wii Menu sounds loaded");
}

void SubstituteWiiMenuSound(const u8*& sound, s32& length, s32& rate) {
    if (!Settings().wii_menu_sounds) return;
    MenuSoundEvent event;
    if (sound == button_over_pcm) event = MenuSoundEvent::Hover;
    else if (sound && sound == button_click_pcm) event = MenuSoundEvent::Click;
    else if (sound && sound == home_open_pcm) event = MenuSoundEvent::HomeOpen;
    else if (sound && sound == home_close_pcm) event = MenuSoundEvent::HomeClose;
    else return;
    const Effect& effect = g_effects[static_cast<unsigned>(event)];
    if (!effect.pcm) return;
    sound = effect.pcm;
    length = effect.bytes;
    rate = WiiSoundPlayer::kRate;
}

void PlayMenuSound(MenuSoundEvent event) {
    if (event >= MenuSoundEvent::Count) return;
    // A pointer prompt can also set its owner's back button to CLICKED on
    // the next menu poll. Those are one action, even across a frame boundary.
    static u64 last_back = 0;
    if (event == MenuSoundEvent::Back) {
        const u64 now = gettime();
        if (last_back && ticks_to_millisecs(now - last_back) < 50) return;
        last_back = now;
    }
    // The theme's click takes priority over extra contextual events too.
    if (Settings().wii_menu_sounds && !ThemeClickSoundFound()) {
        const Effect& effect = g_effects[static_cast<unsigned>(event)];
        // An unavailable contextual cue falls back to RiftWii's PCM itself.
        // Passing its identity through GuiSound would substitute the NAND
        // generic click again, contradicting the per-effect failure log.
        const u8* pcm = effect.pcm ? effect.pcm : button_click_pcm;
        const s32 bytes = effect.pcm ? effect.bytes : button_click_pcm_size;
        const s32 rate = effect.pcm ? WiiSoundPlayer::kRate : 48000;
        if (pcm && bytes > 0 && GuiSound::otherPercent > 0) {
            const s32 voice = ASND_GetFirstUnusedVoice();
            const int volume = 255 * GuiSound::otherPercent / 100;
            if (voice >= 0) {
                ASND_SetVoice(voice, VOICE_STEREO_16BIT, rate, 0,
                              const_cast<u8*>(pcm), bytes, volume, volume, nullptr);
            }
        }
        return;
    }
    GuiSound fallback(button_click_pcm, button_click_pcm_size, SOUND::PCM);
    fallback.Play();
}

std::size_t WiiMenuSoundBytes() { return g_resident_bytes + g_music_player.sample_bytes(); }

bool WiiMenuMusicFound() {
    if (g_music_tried) return g_music_ready;
    g_music_tried = true;
    if (!ArchiveReady()) return false;
    std::string why;
    if (!g_music_player.prepare(g_archive, "WIPL_BGM_MENU", true, why)) {
        logf("Wii Menu music: %s; RiftWii's own music is used\n", why.c_str());
        g_music_player = {};
        return false;
    }
    g_stream = skin::Mem2Alloc(kStreamBytes * 2);
    if (!g_stream) {
        logf("Wii Menu music: there is no MEM2 for the stream buffers; RiftWii's own music is used\n");
        g_music_player = {};
        return false;
    }
    g_resident_bytes += kStreamBytes * 2;
    g_music_ready = true;
    logf("Wii Menu music: loaded WIPL_BGM_MENU (%u decoded sample bytes, %u stream bytes, %u Hz)\n",
         static_cast<unsigned>(g_music_player.sample_bytes()), static_cast<unsigned>(kStreamBytes * 2), WiiSoundPlayer::kRate);
    logf("Wii Menu music: memory budget has %u resident archive, sample, effect and stream bytes; the producer stack is %u bytes\n",
         static_cast<unsigned>(WiiMenuSoundBytes()), static_cast<unsigned>(sizeof g_music_stack));
    mem::LogUsage("Wii Menu music loaded");
    return true;
}

bool WiiMenuMusicStart() {
    if (!WiiMenuMusicFound()) return false;
    WiiMenuMusicStop();
    if (!g_music_player.render(reinterpret_cast<std::int16_t*>(g_stream), kStreamFrames)) {
        logf("Wii Menu music: %s; RiftWii's own music is used\n", g_music_player.error().c_str());
        g_music_ready = false;
        return false;
    }
    DCFlushRange(g_stream, kStreamBytes);
    g_failed.store(false);
    g_running.store(true, std::memory_order_release);
    g_paused.store(false);
    const int volume = g_volume.load();
    if (ASND_SetVoice(0, VOICE_STEREO_16BIT, WiiSoundPlayer::kRate, 0, g_stream, kStreamBytes,
                      volume, volume, StreamCallback) != SND_OK ||
        LWP_CreateThread(&g_music_thread, MusicThread, nullptr, g_music_stack, sizeof g_music_stack, 60) < 0) {
        g_running.store(false);
        ASND_StopVoice(0);
        logf("Wii Menu music: the audio thread could not start; RiftWii's own music is used\n");
        return false;
    }
    return true;
}

bool WiiMenuMusicFailed() {
    if (!g_failed.exchange(false, std::memory_order_acq_rel)) return false;
    g_music_ready = false;
    return true;
}

void WiiMenuMusicStop() {
    g_running.store(false, std::memory_order_release);
    if (g_music_thread != LWP_THREAD_NULL) {
        LWP_JoinThread(g_music_thread, nullptr);
        g_music_thread = LWP_THREAD_NULL;
        ASND_StopVoice(0);
    }
}

void WiiMenuMusicPause(bool pause) {
    g_paused.store(pause);
    if (g_music_thread != LWP_THREAD_NULL) ASND_PauseVoice(0, pause ? 1 : 0);
}

void WiiMenuMusicVolume(int volume) {
    g_volume.store(volume);
    if (g_music_thread != LWP_THREAD_NULL) ASND_ChangeVolumeVoice(0, volume, volume);
}

}  // namespace riftwii::wii
