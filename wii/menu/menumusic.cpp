// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "menumusic.hpp"

#include <asndlib.h>
#include <dirent.h>
#include <gccore.h>
#include <mp3player.h>
#include <ogc/lwp_watchdog.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "audio.h"
#include "homescan.hpp"
#include "loadersettings.hpp"
#include "log.hpp"
#include "menutheme.hpp"
#include "oggplayer.h"
#include "riftwii/checkedalloc.hpp"
#include "riftwii/musiclist.hpp"
#include "riftwii/imagevolume.hpp"
#include "skin.hpp"
#include "usbcatalog.hpp"
#include "wiimenusounds.hpp"

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

// The MEM2 block the song being played is read into, kept for the menu's
// life and used again for the next song (MEM2 is only given back from its
// top, skin::Mem2Release).
u8* g_music = nullptr;
long g_bytes = 0;
std::size_t g_capacity = 0;
std::string g_loaded;  // whose bytes are in the block; "" while none are
MusicFormat g_format = MusicFormat::Ogg;

// RiftWii's own music: the theme's or the card's music.ogg, looked for once.
bool g_tried = false;
std::string g_default_path;

// The player's own pick (settings.txt music_file) and its songs, in the
// order they play: a folder's and a playlist's shuffled.
std::string g_pick;
std::vector<std::string> g_songs;
std::size_t g_song_at = 0;
std::size_t g_scan_remaining = 0;  // a failed batch continues on the next menu service call
bool g_pick_failed = false;     // nothing in the pick plays: RiftWii's own music instead
bool g_waiting_for_usb = false;  // the pick is on the USB drive, which is not read yet

bool g_playing = false;
bool g_using_wii = false;
bool g_using_pick = false;
bool g_kept_for_launch = false;
bool g_mp3_ready = false;
u64 g_song_started = 0;  // when the player was last started, for PlayerEnded
s32 g_resume_at_ms = -1;  // where the track was when a cIOS reload paused it

bool OnUsb(const std::string& path) { return riftwii::is_usb_path(path); }

// Reads `path` whole into the MEM2 block, which grows when the song is
// bigger than the block (to `reserve` at least, so a list's next songs fit
// without growing it again). The player must be stopped: it reads the
// block while it plays. Quiet about a file that is not there when `quiet`.
bool ReadIntoBlock(const std::string& path, std::size_t reserve, bool quiet = false) {
    if (g_music && g_loaded == path) return true;
    std::uint64_t size = 0;
    std::FILE* f = nullptr;
    if (OnUsb(path)) {
        if (!read_usb_range(path, 0, nullptr, 0, size)) {
            if (!quiet) logf("Menu music: %s is not on the USB drive\n", path.c_str());
            return false;
        }
    } else {
        f = std::fopen(path.c_str(), "rb");
        if (!f) {
            if (!quiet) logf("Menu music: %s is not on the SD card\n", path.c_str());
            return false;
        }
        std::fseek(f, 0, SEEK_END);
        const long end = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        size = end > 0 ? static_cast<std::uint64_t>(end) : 0;
    }
    if (size == 0 || size > static_cast<std::uint64_t>(kMaxMusicBytes)) {
        logf("Menu music: %s is %llu bytes; at most %ld are played\n", path.c_str(),
             static_cast<unsigned long long>(size), kMaxMusicBytes);
        if (f) std::fclose(f);
        return false;
    }
    const std::size_t bytes = static_cast<std::size_t>(size);
    if (bytes > g_capacity) {
        // Given back when nothing was taken after it; otherwise it stays
        // taken (MEM2 only grows) and a bigger one is taken above it.
        if (g_music && skin::Mem2Release(g_music, g_capacity)) {
            g_music = nullptr;
            g_capacity = 0;
        }
        std::size_t wanted = std::max(bytes, reserve);
        u8* block = skin::Mem2Alloc(wanted);
        // Reserving for later songs is optional; this song may still fit.
        if (!block && wanted > bytes) {
            wanted = bytes;
            block = skin::Mem2Alloc(wanted);
        }
        if (!block) {
            logf("Menu music: no room in MEM2 for %s (%u bytes)\n", path.c_str(), static_cast<unsigned>(wanted));
            if (f) std::fclose(f);
            return false;
        }
        g_music = block;
        g_capacity = wanted;
    }
    // From here the block's old bytes are gone, whatever the read does.
    g_loaded.clear();
    bool read = false;
    if (f) {
        read = std::fread(g_music, 1, bytes, f) == bytes;
        std::fclose(f);
    } else {
        std::uint64_t whole = 0;
        read = read_usb_range(path, 0, g_music, bytes, whole);
    }
    if (!read) {
        logf("Menu music: cannot read %s\n", path.c_str());
        return false;
    }
    g_bytes = static_cast<long>(bytes);
    g_loaded = path;
    return true;
}

// RiftWii's own music: the theme's music.ogg (docs/THEMES.md), then the
// card's, then the one RiftWii ships, read into the block.
bool LoadDefault() {
    if (!g_tried) {
        g_tried = true;
        const std::string theme_music = MenuThemeMusic();
        std::vector<std::string> paths;
        if (!theme_music.empty()) paths.push_back(theme_music);
        for (const char* path : kMusicFiles) paths.push_back(path);
        for (const std::string& path : paths) {
            if (!ReadIntoBlock(path, 0, true)) continue;
            g_default_path = path;
            logf("Menu music: %s (%ld bytes)\n", path.c_str(), g_bytes);
            break;
        }
    }
    if (g_default_path.empty() || !ReadIntoBlock(g_default_path, 0)) return false;
    g_format = MusicFormat::Ogg;
    return true;
}

// One folder of the SD card (opendir) or of the USB drive (the menu's
// own view of it, read only), for the folder pick.
bool ListFolder(const std::string& folder, std::vector<MusicFolderEntry>& out) {
    out.clear();
    if (OnUsb(folder)) {
        std::vector<VolumeEntry> entries;
        if (!list_usb_folder(folder, entries)) return false;
        for (const VolumeEntry& entry : entries) out.push_back({entry.name, entry.is_directory});
        return true;
    }
    DIR* dir = opendir(folder.c_str());
    if (!dir) return false;
    const std::string base = folder.back() == '/' ? folder : folder + "/";
    while (dirent* entry = readdir(dir)) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") continue;
        struct stat st;
        const bool is_dir = stat((base + name).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
        out.push_back({name, is_dir});
    }
    closedir(dir);
    std::sort(out.begin(), out.end(),
              [](const MusicFolderEntry& a, const MusicFolderEntry& b) { return a.name < b.name; });
    return true;
}

bool ReadPlaylist(const std::string& path, std::string& text) {
    text.clear();
    if (OnUsb(path)) {
        std::uint64_t size = 0;
        if (!read_usb_range(path, 0, nullptr, 0, size) || size > kMaxMusicPlaylistBytes) return false;
        if (!try_resize(text, static_cast<std::size_t>(size))) return false;
        if (text.empty()) return true;
        std::uint64_t whole = 0;
        if (!read_usb_range(path, 0, reinterpret_cast<u8*>(&text[0]), text.size(), whole) || whole != size) {
            text.clear();
            return false;
        }
        return true;
    }
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    const bool read = music_read_playlist(f, text);
    std::fclose(f);
    return read;
}

std::uint32_t ShuffleSeed() { return static_cast<std::uint32_t>(gettime()); }

// The pick's songs, shuffled when there are several. False when it has none.
bool BuildSongs(const std::string& pick) {
    g_songs.clear();
    g_song_at = 0;
    if (music_choice_is_folder(pick)) {
        g_songs = music_folder_songs(pick, ListFolder);
    } else if (music_is_playlist(pick)) {
        std::string text;
        if (ReadPlaylist(pick, text)) g_songs = music_playlist_songs(text, pick);
        else logf("Menu music: cannot read the playlist %s\n", pick.c_str());
    } else {
        g_songs.push_back(pick);
    }
    if (g_songs.size() > 1) music_shuffle(g_songs, ShuffleSeed());
    logf("Menu music: %s has %u song(s)%s\n", pick.c_str(), static_cast<unsigned>(g_songs.size()),
         g_songs.size() > 1 ? ", shuffled" : "");
    return !g_songs.empty();
}

// A list's songs play once each and the menu moves on to the next (one
// song alone goes round and round). The MP3 player only plays a song
// through once, so a lone MP3 is started again when it ends.
bool PickLoops() { return g_songs.size() == 1; }

void PlayerStop() {
    if (g_format == MusicFormat::Mp3) {
        if (g_mp3_ready) MP3Player_Stop();
    } else {
        StopOgg();
    }
}

bool PlayerStart(s32 at_ms) {
    if (g_format == MusicFormat::Mp3) {
        if (!g_mp3_ready) {
            MP3Player_Init();
            g_mp3_ready = true;
        }
        // libogc's MP3 player cannot start part way: a song paused for a
        // cIOS reload starts again from the top.
        g_song_started = gettime();
        return MP3Player_PlayBuffer(g_music, g_bytes, nullptr) == 0;
    }
    const int mode = g_using_pick && !PickLoops() ? OGG_ONE_TIME : OGG_INFINITE_TIME;
    g_song_started = gettime();
    return PlayOgg(g_music, g_bytes, at_ms, mode) >= 0;
}

// Whether the song came to its end (not paused, not stopped by the menu).
bool PlayerEnded() {
    // The MP3 player says it plays only once its thread has begun, so a
    // song just started is not taken for one that ended.
    if (ticks_to_millisecs(gettime() - g_song_started) < 1000) return false;
    if (g_format == MusicFormat::Mp3) return g_mp3_ready && !MP3Player_IsPlaying();
    // The Ogg decoder is done at the end of the file; the mixer still has
    // its last piece to play.
    return StatusOgg() == OGG_STATUS_EOF && ASND_StatusVoice(0) == SND_UNUSED;
}

void MusicVolume(int volume) {
    if (g_using_wii) WiiMenuMusicVolume(volume);
    else if (g_format == MusicFormat::Mp3) MP3Player_Volume(static_cast<u32>(volume));
    else SetVolumeOgg(volume);
}

// The half-second fade both endings of the music share, so they cannot drift
// apart: the volume steps down to silence, the decoder keeps running.
void FadeVolumeToZero() {
    for (int step = kFadeSteps - 1; step >= 0; --step) {
        MusicVolume(kVolume * step / kFadeSteps);
        usleep(kFadeStepUs);
    }
}

void BeginPickScan() {
    if (g_song_at >= g_songs.size()) {
        // A playback round is over. Keep this order for the entire scan,
        // so wrapping after failed files cannot skip an untried song.
        const std::string last = g_songs.empty() ? "" : g_songs.back();
        if (g_songs.size() > 1) music_shuffle(g_songs, ShuffleSeed(), last);
        g_song_at = 0;
    }
    g_scan_remaining = g_songs.size();
}

// Reads the pick's song at g_song_at and starts it; on failure the songs
// after it, so one damaged file does not silence a whole folder. Each
// call tries at most eight; g_scan_remaining keeps the rest for later.
bool PlayPickSong() {
    const bool played = music_try_song_batch(g_scan_remaining, []() {
        if (g_song_at >= g_songs.size()) g_song_at = 0;
        const std::string& song = g_songs[g_song_at];
        const MusicFormat format = music_format(song);
        if (format != MusicFormat::None && ReadIntoBlock(song, g_songs.size() > 1 ? kMaxMusicBytes : 0)) {
            g_format = format;
            if (PlayerStart(0)) {
                logf("Menu music: playing %s (%ld bytes)\n", song.c_str(), g_bytes);
                return true;
            }
            logf("Menu music: the player refused %s\n", song.c_str());
        }
        ++g_song_at;
        return false;
    });
    if (!played && g_scan_remaining == 0) {
        g_pick_failed = true;
        logf("Menu music: none of %s plays; RiftWii's own music instead\n", g_pick.c_str());
    }
    return played;
}

// Starts the player's pick. False when there is none or it cannot play
// (the caller falls back to RiftWii's own); `waiting` when it is on the
// USB drive and the drive is not read yet.
bool StartPick(bool& waiting) {
    waiting = false;
    const std::string& pick = Settings().music_file;
    if (pick != g_pick) {
        g_pick = pick;
        g_songs.clear();
        g_scan_remaining = 0;
        g_pick_failed = false;
    }
    if (pick.empty()) return false;
    if (g_pick_failed) return false;
    if (OnUsb(pick) && (!DriveScanDone() || !usb_volume_ready())) {
        // Before Home's first look at the drives (the menu starts its
        // music first) or while one runs, the drive is not the menu's to
        // read. Once a look has finished without a drive, there is none.
        if (!DriveScanFinished()) {
            waiting = true;
            return false;
        }
        logf("Menu music: %s is on the USB drive, and there is no drive; RiftWii's own music instead\n", pick.c_str());
        g_pick_failed = true;
        return false;
    }
    if (g_songs.empty() && !BuildSongs(pick)) {
        g_pick_failed = true;
        return false;
    }
    // A scan already started gets its next batch only from the menu
    // service, even if fallback music has to be restarted meanwhile.
    if (g_scan_remaining > 0) return false;
    BeginPickScan();
    g_using_pick = true;
    if (!PlayPickSong()) {
        g_using_pick = false;
        return false;
    }
    return true;
}

void StartFallback() {
    g_using_wii = Settings().wii_menu_music && MenuThemeMusic().empty() && WiiMenuMusicStart();
    if (g_using_wii) {
        WiiMenuMusicVolume(kVolume);
        g_playing = true;
        return;
    }
    if (!LoadDefault()) return;
    if (!PlayerStart(0)) {
        logf("Menu music: the player refused the file\n");
        return;
    }
    SetVolumeOgg(kVolume);
    g_playing = true;
}

// Internal batch changes stop the current player while keeping the scan.
// A public stop cancels the scan too, so it cannot restart after a fade.
void StopPlayback() {
    g_kept_for_launch = false;
    g_resume_at_ms = -1;
    g_waiting_for_usb = false;
    if (!g_playing) return;
    if (g_using_wii) WiiMenuMusicStop();
    else PlayerStop();
    g_playing = false;
}

}  // namespace

void MenuMusicStart() {
    if (g_playing || Settings().menu_music == "off") return;
    // The player's own pick first, then a theme's music, then the selected
    // NAND source, then the card's and RiftWii's own Ogg files.
    bool waiting = false;
    g_using_wii = false;
    g_using_pick = false;
    if (StartPick(waiting)) {
        MusicVolume(kVolume);
        g_playing = true;
        return;
    }
    g_waiting_for_usb = waiting;
    if (waiting) return;  // MenuMusicService starts it once the drive is read
    StartFallback();
}

void MenuMusicUpdate() {
    if (!g_using_wii || !WiiMenuMusicFailed()) return;
    WiiMenuMusicStop();
    g_using_wii = false;
    g_playing = false;
    MenuMusicStart();
}

void MenuMusicService() {
    // Not under a launch: the launch reads the drives itself, and the
    // music it kept plays on as it is.
    if (g_kept_for_launch || Settings().menu_music == "off") return;
    if (g_waiting_for_usb) {
        if (DriveScanDone() && (usb_volume_ready() || DriveScanFinished())) {
            g_waiting_for_usb = false;
            MenuMusicStart();
        }
        return;
    }
    if (g_scan_remaining == 0 && (!g_playing || !g_using_pick || !PlayerEnded())) return;
    // Home's look at the drives owns the USB drive while it runs (Settings
    // can start one): the next song waits for it.
    if (OnUsb(g_pick) && (!DriveScanDone() || !usb_volume_ready())) return;
    if (g_scan_remaining == 0 && PickLoops()) {
        // A lone MP3 at its end (a lone Ogg goes round by itself).
        if (PlayerStart(0)) MusicVolume(kVolume);
        return;
    }
    // The next song. The GUI is parked meanwhile, so the screen holds
    // still for the moment the song takes to read.
    if (g_scan_remaining == 0) {
        ++g_song_at;
        BeginPickScan();
    }
    // A pending batch may be running over fallback music. Stop that
    // player before any song overwrites its MEM2 buffer.
    StopPlayback();
    g_using_wii = false;
    g_using_pick = true;
    if (PlayPickSong()) {
        MusicVolume(kVolume);
        g_playing = true;
        return;
    }
    g_using_pick = false;
    StartFallback();
}

void MenuMusicForgetPick() {
    g_pick_failed = false;
    g_songs.clear();
    g_scan_remaining = 0;
    // The block's bytes may be from a card that is gone, at the same path as
    // a file on this one: read again. The callers stop the music first.
    g_loaded.clear();
}

bool MenuMusicRetryFailedPick() {
    // A pick that plays keeps its songs and its place among them.
    if (!g_pick_failed) return false;
    MenuMusicForgetPick();
    return true;
}

void MenuMusicStop() {
    StopPlayback();
    g_scan_remaining = 0;
}

void MenuMusicPause(bool pause) {
    if (g_using_wii) WiiMenuMusicPause(pause);
    // The MP3 player has no pause of its own; its voice (0) is held, and
    // its decoder waits for the voice to want more.
    else if (g_format == MusicFormat::Mp3) ASND_PauseVoice(0, pause ? 1 : 0);
    else PauseOgg(pause ? 1 : 0);
}

void MenuMusicFadeOut() {
    if (g_playing) FadeVolumeToZero();
    MenuMusicStop();
}

bool MenuMusicFound() {
    if (g_using_pick && g_playing) return true;
    if (Settings().wii_menu_music && MenuThemeMusic().empty() && WiiMenuMusicFound()) return true;
    // Looked for once; the block holds no playing song here (above).
    if (!g_tried) LoadDefault();
    return !g_default_path.empty();
}

std::string MenuMusicPickProblem() {
    const std::string& pick = Settings().music_file;
    if (pick.empty() || pick != g_pick) return "";
    if (g_waiting_for_usb) return "waiting";
    return g_pick_failed ? "failed" : "";
}

std::vector<MusicFolderEntry> MenuMusicBrowse(const std::string& folder) {
    std::vector<MusicFolderEntry> folders, files;
    std::vector<MusicFolderEntry> entries;
    if (!ListFolder(folder, entries)) return folders;
    for (const MusicFolderEntry& entry : entries) {
        // Hidden ones and a Mac's "._" copies are left out.
        if (entry.name.empty() || entry.name[0] == '.') continue;
        if (entry.is_directory) {
            if (folders.size() < 300) folders.push_back(entry);
        } else if ((music_format(entry.name) != MusicFormat::None || music_is_playlist(entry.name)) &&
                   files.size() < 500) {
            files.push_back(entry);
        }
    }
    folders.insert(folders.end(), files.begin(), files.end());
    return folders;
}

std::size_t MenuMusicFolderSongs(const std::string& folder) { return music_folder_songs(folder, ListFolder).size(); }

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
    // is halted from here on, nor the card, which the launch reads. For
    // the same reason a list does not move on to its next song under the
    // launch: the song playing now plays to its end.
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
    const s32 at_ms = g_using_wii || g_format == MusicFormat::Mp3 ? 0 : GetTimeOgg();
    FadeOutAndStop(why, at_ms >= 0 ? at_ms : 0);
}

void LaunchMusicResumeAfterReload() {
    if (g_resume_at_ms < 0) return;
    const s32 at_ms = g_resume_at_ms;
    g_resume_at_ms = -1;
    InitAudio();
    // The MP3 player was stopped with the audio; it starts afresh.
    g_mp3_ready = false;
    const bool resumed = g_using_wii ? WiiMenuMusicStart() : PlayerStart(at_ms);
    if (!resumed) {
        ShutdownAudio();
        logf("Menu music: not resumed after the reload (the player refused the file)\n");
        return;
    }
    // PlayOgg starts at full volume; its thread hands the mixer nothing
    // before its second decode, after it has yielded back to here.
    MusicVolume(0);
    g_playing = true;
    g_kept_for_launch = true;
    for (int step = 1; step <= kFadeInSteps; ++step) {
        MusicVolume(kVolume * step / kFadeInSteps);
        usleep(kFadeStepUs);
    }
    if (g_using_wii) {
        logf("Menu music: the System Menu sequence continued after the reload\n");
    } else if (g_format == MusicFormat::Mp3) {
        logf("Menu music: the MP3 started again after the reload\n");
    } else {
        logf("Menu music: back after the reload, %d.%01d s into the track\n", static_cast<int>(at_ms / 1000),
             static_cast<int>(at_ms % 1000 / 100));
    }
}

}  // namespace riftwii::wii
