// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Video settings applied to a game before it starts. A Wii game describes
// each picture it can output in a render mode table (the SDK's
// GXRModeObj, 60 bytes: TV mode, frame buffer sizes, where the picture
// sits in the TV signal, the anti-aliasing pattern and the vertical
// "deflicker" filter). Those tables are data in the loaded DOL, so they
// can be found and rewritten before the game reads them:
//   - Width: how wide the picture is drawn in the 720-pixel TV line. Many
//     games draw 640 pixels and leave black bars at the sides, which old
//     TVs hid (overscan) and modern ones show.
//   - Borders: removing them stretches the picture over the bars at the
//     sides (the VI's width, which the game never sees). "Remove all" also
//     stretches it over the bars at the top and bottom: the VI cannot scale
//     vertically, so the tables' frame buffer heights change and the game's
//     copy to the frame buffer scales to them. Games that use those heights
//     for anything else break (issue 15), so that one is experimental.
//   - Deflicker: the filter that blends neighbouring lines to hide
//     interlace flicker. It blurs; turning it off gives a sharp picture.
//   - Mode: the TV format the game outputs (NTSC, PAL 50 Hz, PAL 60 Hz,
//     480p). Each table the SDK defines is converted to the same kind of
//     table in the other format, as that patcher does; a table with
//     unusual heights keeps its own format (it still works, unforced).
//   - 240p, for CRT TVs and pixel art: a table that draws a whole
//     480-line picture (interlaced or 480p) becomes a double-strike one
//     whose frame buffer is 240 lines. The SDK sets the game's copy to
//     the frame buffer from the table's heights, so the game itself
//     copies every other line of its picture, and the VI sends those
//     240 lines progressive at 60 Hz, as old consoles did. No line is
//     blended with its neighbours (the flicker filter is off: there is
//     no interlace flicker to hide). Field-rendered, double-strike and
//     unusual tables stay as made.
namespace riftwii {

enum class VideoWidth { Game, Framebuffer, W704, W720 };
enum class Deflicker { Game, Off, Low, Medium, High };
// As chosen: "system" is the console's own setting, resolved on the Wii
// into a VideoTarget.
enum class VideoMode { Game, System, Ntsc, Pal60, Pal50, Progressive, Line240 };

// The VI's TV formats (libogc's VI_NTSC ...).
constexpr int kViNtsc = 0, kViPal = 1, kViMpal = 2, kViEurgb60 = 5;

struct VideoTarget {
    int format = -1;           // -1: the game's own tables are kept (with line240: their own formats)
    bool progressive = false;  // interlaced tables become 480p ones
    bool line240 = false;      // whole 480-line frames become 240p (double-strike) ones
};

struct VideoSettings {
    VideoWidth width = VideoWidth::Game;
    Deflicker deflicker = Deflicker::Game;
    bool remove_borders = false;  // the side bars: implies the full 720-pixel width
    bool remove_top_bottom = false;  // with remove_borders: the full height too (experimental)
    VideoMode mode = VideoMode::Game;
    VideoTarget target;           // what `mode` means on this console
    bool any() const {
        return width != VideoWidth::Game || deflicker != Deflicker::Game || remove_borders || target.format >= 0 ||
               target.line240;
    }
};

// Names as saved and shown: "game", "framebuffer", "704", "720" and
// "game", "off", "low", "medium", "high".
const char* to_string(VideoWidth w);
const char* to_string(Deflicker d);
// "game", "system", "ntsc", "pal60", "pal50", "480p", "240p".
const char* to_string(VideoMode m);
bool parse_video_width(const std::string& s, VideoWidth& out);
bool parse_deflicker(const std::string& s, Deflicker& out);
bool parse_video_mode(const std::string& s, VideoMode& out);

struct VideoPatchReport {
    unsigned modes = 0;         // render mode tables found
    unsigned patched = 0;       // of those, changed
    unsigned converted = 0;     // of those, moved to the target format
    bool side_borders = false;  // a mode draws narrower than the TV line
    bool top_borders = false;   // a mode leaves lines of the TV picture unused
    std::string describe() const;
};

// Games whose picture breaks when the VI width of their render mode tables
// changes (Picture width, or Black borders: Remove, which widens it to 720):
// they keep their own width and borders. Mario Super Sluggers (RMB): a
// tester's screen went black but for a strip of one colour and a green
// square, the game running underneath; either setting alone did it.
bool game_keeps_picture_width(const std::string& game_id);

// Games whose picture breaks when Black borders: Remove all gives their
// frame buffer the TV's whole height: they keep their top and bottom
// borders (the side ones still go). Kirby's Return to Dream Land (SUK)
// and Dream Collection (SUZ) show a pink bar under the picture (#13,
// reproduced in Dolphin); Mario Party 8 (RM8) a shifted, half-drawn
// minigame screen, Resident Evil 4 (RB4) a flashing title and a crash,
// Link's Crossbow Training (RZP) misbehaved (#15).
bool game_keeps_picture_height(const std::string& game_id);

// Finds the render mode tables in `bytes` (a loaded DOL section, 4-byte
// aligned) and applies `settings` to them, adding to `report`.
void patch_video_modes(std::uint8_t* bytes, std::size_t size, const VideoSettings& settings, VideoPatchReport& report);

}  // namespace riftwii
