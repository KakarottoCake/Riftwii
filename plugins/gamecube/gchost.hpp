// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <gctypes.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"
#include "riftwii/gcoverlay.hpp"
#include "riftwii/plugin_api.h"
#include "riftwii/rvz.hpp"
#include "rwpruntime.hpp"

// What the GameCube loader (gcnative.cpp) had from the DOL before it moved
// into the gamecube plugin, now from the request (gcentry.cpp) and
// RiftWii's table (gchost.cpp). The helpers keep the names and C++
// signatures the moved code calls, so the move changes as few of its
// lines as it can; each is RiftWii's own function behind the table.
namespace riftwii::wii {

// The game, as the DOL's GcImage (wii/launch/gclaunch.hpp) described it.
struct GcImage {
    std::string path;  // sd:/games/.../game.iso, or game.rvz
    std::string id;    // ID6 from its header
    bool rvz = false;  // the image is an RVZ
    bool shared_card = false;
    bool drive_timing = true;
    bool progressive = false;
    bool widescreen = false;
    int language = -1;
    bool sharp = false;
    bool sixty_hz = false;
    std::uint32_t vi_width = 0;
};

// What the launch read from the settings, the launch's extras, the crash
// screen and the way back in the DOL (riftwii/plugin_gamecube.h).
struct GcLaunchExtras {
    bool rumble_off = false;
    bool in_dolphin = false;
    std::vector<std::uint8_t> cheat_gct;
    std::size_t cheat_count = 0;
    bool code_build_codes = false;
    const std::uint8_t* code_handler = nullptr;
    std::size_t code_handler_bytes = 0;
    const std::uint8_t* crash_screen = nullptr;
    std::size_t crash_screen_bytes = 0;
    bool crash_screen_way_back = false;
    u64 way_back_title = 0;  // 0: none
    const void* way_back_view = nullptr;
    std::size_t way_back_view_bytes = 0;
};

// Starts the game (gcnative.cpp). Returns only on failure.
bool LaunchGcNative(const GcImage& image, const GcOverlayPlan* pack, const GcLaunchExtras& extras,
                    std::string& error);

// ---- RiftWii's functions the loader calls, on the table ----

// A line in boot.log, as the DOL's logf writes it.
void logf(const char* format, ...) __attribute__((format(printf, 1, 2)));
// Ends the log file (before the SD card is handed over).
void LogClose();
// An SD file's sectors (wii/storage/sdfile.hpp).
bool resolve_sd_file(const std::string& sd_path, Fat32File& out, std::string& error);
// After a file was written through the mount (sdfile.hpp).
void forget_sd_layout();
// The SD card let go by the file system and opened for the game.
bool hand_over_sd_card(rwp_sd_card& card, std::string& error);
// Data written for a DMA out to memory, and code written made runnable
// (libogc's DCFlushRange and ICInvalidateRange; the latter flushes too).
inline void DCFlushRange(void* start, u32 bytes) { plugin::host().flush_range(start, bytes); }
inline void ICInvalidateRange(void* start, u32 bytes) { plugin::host().sync_code(start, bytes); }
// The time base, as libogc's gettime reads it (its header would bring all
// of libogc's inline functions, which call into libogc): the upper half
// read again until it did not change between the two reads.
inline u64 gettime() {
    u32 upper = 0;
    u32 lower = 0;
    u32 again = 0;
    do {
        asm volatile("mftbu %0" : "=r"(upper));
        asm volatile("mftb %0" : "=r"(lower));
        asm volatile("mftbu %0" : "=r"(again));
    } while (upper != again);
    return (static_cast<u64>(upper) << 32) | lower;
}

// The game's disc, as the DOL's OpenGcDisc opened it: the file itself, or
// for an RVZ the disc it holds, then also in `rvz_disc` (the plugin has
// no RTTI to tell the two apart afterwards).
bool open_gc_disc(const std::string& path, bool rvz, std::unique_ptr<ByteSource>& out,
                  const RvzDiscSource*& rvz_disc, std::string& error);

// False, with `error`, when the plugin's own code lies within one arena's
// size of [start, end): a launch that writes there would overwrite the
// code doing it (docs/PLUGINS.md, "Stage (b)").
bool plugin_clear_of(std::uint32_t start, std::uint32_t end, std::string& error);

}  // namespace riftwii::wii
