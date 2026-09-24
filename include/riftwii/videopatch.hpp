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
//   - Borders: some games also leave bars at the top and bottom; removing
//     them stretches the picture to the full height.
//   - Deflicker: the filter that blends neighbouring lines to hide
//     interlace flicker. It blurs; turning it off gives a sharp picture.
//   - Mode: the TV format the game outputs (NTSC, PAL 50 Hz, PAL 60 Hz,
//     480p). Each table the SDK defines is converted to the same kind of
//     table in the other format, as USB Loader GX does; a table with
//     unusual heights keeps its own format (it still works, unforced).
namespace riftwii {

enum class VideoWidth { Game, Framebuffer, W704, W720 };
enum class Deflicker { Game, Off, Low, Medium, High };
// As chosen: "system" is the console's own setting, resolved on the Wii
// into a VideoTarget.
enum class VideoMode { Game, System, Ntsc, Pal60, Pal50, Progressive };

// The VI's TV formats (libogc's VI_NTSC ...).
constexpr int kViNtsc = 0, kViPal = 1, kViMpal = 2, kViEurgb60 = 5;

struct VideoTarget {
    int format = -1;           // -1: the game's own tables are kept
    bool progressive = false;  // interlaced tables become 480p ones
};

struct VideoSettings {
    VideoWidth width = VideoWidth::Game;
    Deflicker deflicker = Deflicker::Game;
    bool remove_borders = false;  // implies the full 720-pixel width
    VideoMode mode = VideoMode::Game;
    VideoTarget target;           // what `mode` means on this console
    bool any() const {
        return width != VideoWidth::Game || deflicker != Deflicker::Game || remove_borders || target.format >= 0;
    }
};

// Names as saved and shown: "game", "framebuffer", "704", "720" and
// "game", "off", "low", "medium", "high".
const char* to_string(VideoWidth w);
const char* to_string(Deflicker d);
// "game", "system", "ntsc", "pal60", "pal50", "480p".
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

// Finds the render mode tables in `bytes` (a loaded DOL section, 4-byte
// aligned) and applies `settings` to them, adding to `report`.
void patch_video_modes(std::uint8_t* bytes, std::size_t size, const VideoSettings& settings, VideoPatchReport& report);

}  // namespace riftwii
