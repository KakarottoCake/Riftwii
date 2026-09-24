// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/videopatch.hpp"

namespace riftwii {
namespace {

// GXRModeObj (libogc gx_struct.h), big endian.
constexpr std::size_t kModeBytes = 60;
constexpr std::size_t kTvMode = 0, kFbWidth = 4, kEfbHeight = 6, kXfbHeight = 8, kViX = 10, kViY = 12, kViWidth = 14,
                      kViHeight = 16, kXfbMode = 20, kField = 24, kAa = 25, kPattern = 26, kFilter = 50;
constexpr unsigned kLine = 720;  // the TV line the VI can fill

std::uint32_t be32(const std::uint8_t* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
unsigned be16(const std::uint8_t* p) { return (unsigned(p[0]) << 8) | p[1]; }
void put16(std::uint8_t* p, unsigned v) {
    p[0] = static_cast<std::uint8_t>(v >> 8);
    p[1] = static_cast<std::uint8_t>(v);
}

// VI_TVMODE(format, mode): formats NTSC, PAL, MPAL, DEBUG, DEBUG_PAL,
// EURGB60; modes interlaced, non-interlaced ("DS"), progressive.
bool tv_mode_ok(std::uint32_t m) { return m < 24 && (m & 3) != 3; }
bool pal_lines(std::uint32_t m) { return (m >> 2) == 1 || (m >> 2) == 4; }  // 576-line formats
bool double_strike(std::uint32_t m) { return (m & 3) == 1; }

// The lines of the TV picture a mode can fill.
unsigned full_height(std::uint32_t tv_mode) {
    const unsigned lines = pal_lines(tv_mode) ? 574 : 480;
    return double_strike(tv_mode) ? lines / 2 : lines;
}

void put32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

void set_filter(std::uint8_t* p, Deflicker d) {
    static const std::uint8_t kOff[7] = {0, 0, 21, 22, 21, 0, 0};
    static const std::uint8_t kLow[7] = {4, 4, 16, 16, 16, 4, 4};
    static const std::uint8_t kMedium[7] = {7, 7, 12, 12, 12, 7, 7};
    static const std::uint8_t kHigh[7] = {8, 8, 10, 12, 10, 8, 8};
    const std::uint8_t* f = d == Deflicker::Off ? kOff : d == Deflicker::Low ? kLow : d == Deflicker::Medium ? kMedium : kHigh;
    for (std::size_t i = 0; i < 7; ++i) p[kFilter + i] = f[i];
}

// The heights of the SDK's tables (libogc's video.c, the Wii SDK's PAL
// ones as USB Loader GX lists them): efb, xfb, VI origin and VI height
// for each kind of table, in 480- and 576-line formats.
struct Heights {
    unsigned efb, xfb, y, h;
};
enum Kind { kFull, kFullAa, kHalf, kDoubleStrike, kKinds };
constexpr Heights kLines480[kKinds] = {{480, 480, 0, 480}, {242, 480, 0, 480}, {240, 240, 0, 480}, {240, 240, 0, 480}};
constexpr Heights kLines576[kKinds] = {{528, 528, 23, 528}, {264, 524, 23, 524}, {264, 264, 23, 528}, {264, 264, 11, 528}};

// Which SDK table kind `p` is, when its heights are exactly one (the VI
// origin may differ by a line: libogc and the SDK round differently).
bool kind_of(const std::uint8_t* p, Kind& out) {
    const std::uint32_t tv = be32(p + kTvMode);
    Kind kind = kFull;
    if (double_strike(tv)) kind = kDoubleStrike;
    else if (p[kField] == 1) kind = kHalf;
    else if (p[kAa] == 1) kind = kFullAa;
    const Heights& want = pal_lines(tv) ? kLines576[kind] : kLines480[kind];
    const unsigned y = be16(p + kViY);
    if (be16(p + kEfbHeight) != want.efb || be16(p + kXfbHeight) != want.xfb || be16(p + kViHeight) != want.h) {
        // The SDK's scaled PAL table (480 lines stretched over 576).
        if (!(pal_lines(tv) && kind == kFull && be16(p + kEfbHeight) == 480 && be16(p + kXfbHeight) == 576 &&
              be16(p + kViHeight) == 576 && y == 0)) {
            return false;
        }
    } else if (y != want.y && y != want.y + 1) {
        return false;
    }
    out = kind;
    return true;
}

// Moves one table to `target`. Returns whether it changed.
bool convert(std::uint8_t* p, const VideoTarget& target) {
    const std::uint32_t tv = be32(p + kTvMode);
    std::uint32_t mode = tv & 3;
    const bool from576 = pal_lines(tv);
    const bool to576 = target.format == kViPal;
    Kind kind = kFull;
    const bool standard = kind_of(p, kind);
    if (from576 != to576 && !standard) return false;  // unknown heights: leave it whole
    // 480p only exists in 480 lines; field-rendered and double-strike
    // tables have no progressive form and stay as they are.
    const bool full = kind == kFull || kind == kFullAa;
    const bool to_progressive = target.progressive && !to576 && mode == 0 && full && standard;
    const bool to_interlaced = mode == 2 && (!target.progressive || to576);
    if (from576 != to576 || to_progressive) {
        const Heights& h = to576 ? kLines576[kind] : kLines480[kind];
        put16(p + kEfbHeight, h.efb);
        put16(p + kXfbHeight, h.xfb);
        put16(p + kViY, h.y);
        put16(p + kViHeight, h.h);
    }
    if (to_progressive) {
        mode = 2;
        put32(p + kXfbMode, 0);  // single field
        set_filter(p, Deflicker::Off);
    } else if (to_interlaced) {
        mode = 0;
        put32(p + kXfbMode, full ? 1 : 0);  // full frames: double field
        set_filter(p, Deflicker::Medium);
    }
    const std::uint32_t now = (static_cast<std::uint32_t>(target.format) << 2) | mode;
    const bool changed = now != tv || from576 != to576 || to_progressive || to_interlaced;
    put32(p + kTvMode, now);
    return changed;
}

// Whether `p` holds a render mode table: every field in the range the SDK
// allows, and the filter's taps summing to 64 as every SDK table does.
bool is_mode(const std::uint8_t* p) {
    const std::uint32_t tv = be32(p + kTvMode);
    if (!tv_mode_ok(tv)) return false;
    const unsigned fb = be16(p + kFbWidth), efb = be16(p + kEfbHeight), xfb = be16(p + kXfbHeight);
    const unsigned x = be16(p + kViX), y = be16(p + kViY), w = be16(p + kViWidth), h = be16(p + kViHeight);
    if (fb < 320 || fb > kLine || fb % 16 != 0) return false;
    if (efb < 200 || efb > 528 || xfb < 200 || xfb > 576) return false;
    if (w < 320 || w > kLine || x + w > kLine || w % 2 != 0) return false;
    // Double-strike tables give the VI the full frame height too.
    const unsigned full = double_strike(tv) ? full_height(tv) * 2 : full_height(tv);
    if (h < full / 2 || y + h > full + 2) return false;  // PAL tables say 574 or 576
    if (p[kXfbMode - 2] != 0 || p[kXfbMode - 1] != 0) return false;  // the padding before xfbMode
    if (be32(p + kXfbMode) > 1 || p[kField] > 1 || p[kAa] > 1) return false;
    for (std::size_t i = 0; i < 24; ++i) {
        if (p[kPattern + i] > 12) return false;
    }
    unsigned taps = 0;
    for (std::size_t i = 0; i < 7; ++i) taps += p[kFilter + i];
    return taps == 64;
}


}  // namespace

const char* to_string(VideoWidth w) {
    switch (w) {
        case VideoWidth::Framebuffer: return "framebuffer";
        case VideoWidth::W704: return "704";
        case VideoWidth::W720: return "720";
        default: return "game";
    }
}

const char* to_string(Deflicker d) {
    switch (d) {
        case Deflicker::Off: return "off";
        case Deflicker::Low: return "low";
        case Deflicker::Medium: return "medium";
        case Deflicker::High: return "high";
        default: return "game";
    }
}

const char* to_string(VideoMode m) {
    switch (m) {
        case VideoMode::System: return "system";
        case VideoMode::Ntsc: return "ntsc";
        case VideoMode::Pal60: return "pal60";
        case VideoMode::Pal50: return "pal50";
        case VideoMode::Progressive: return "480p";
        default: return "game";
    }
}

bool parse_video_mode(const std::string& s, VideoMode& out) {
    for (VideoMode m : {VideoMode::Game, VideoMode::System, VideoMode::Ntsc, VideoMode::Pal60, VideoMode::Pal50,
                        VideoMode::Progressive}) {
        if (s == to_string(m)) {
            out = m;
            return true;
        }
    }
    return false;
}

bool parse_video_width(const std::string& s, VideoWidth& out) {
    for (VideoWidth w : {VideoWidth::Game, VideoWidth::Framebuffer, VideoWidth::W704, VideoWidth::W720}) {
        if (s == to_string(w)) {
            out = w;
            return true;
        }
    }
    return false;
}

bool parse_deflicker(const std::string& s, Deflicker& out) {
    for (Deflicker d : {Deflicker::Game, Deflicker::Off, Deflicker::Low, Deflicker::Medium, Deflicker::High}) {
        if (s == to_string(d)) {
            out = d;
            return true;
        }
    }
    return false;
}

std::string VideoPatchReport::describe() const {
    std::string s = std::to_string(modes) + " video mode(s), " + std::to_string(patched) + " changed";
    if (converted != 0) s += " (" + std::to_string(converted) + " to the chosen TV format)";
    if (side_borders || top_borders) {
        s += "; the game draws black borders";
        s += side_borders && top_borders ? " at the sides and top" : side_borders ? " at the sides" : " at the top";
    }
    return s;
}

void patch_video_modes(std::uint8_t* bytes, std::size_t size, const VideoSettings& settings, VideoPatchReport& report) {
    for (std::size_t at = 0; at + kModeBytes <= size; at += 4) {
        std::uint8_t* p = bytes + at;
        if (!is_mode(p)) continue;
        ++report.modes;
        bool changed = false;
        if (settings.target.format >= 0 && convert(p, settings.target)) {
            ++report.converted;
            changed = true;
        }
        const std::uint32_t tv = be32(p + kTvMode);
        const unsigned fb = be16(p + kFbWidth);
        const unsigned w = be16(p + kViWidth), h = be16(p + kViHeight);
        const unsigned full = full_height(tv);
        if (w < 704) report.side_borders = true;
        if (h + 8 < full) report.top_borders = true;
        VideoWidth width = settings.remove_borders ? VideoWidth::W720 : settings.width;
        unsigned new_w = w;
        if (width == VideoWidth::Framebuffer) new_w = fb;
        if (width == VideoWidth::W704) new_w = 704;
        if (width == VideoWidth::W720) new_w = kLine;
        if (new_w != w) {
            put16(p + kViWidth, new_w);
            put16(p + kViX, (kLine - new_w) / 2);
            changed = true;
        }
        if (settings.remove_borders && h < full && !double_strike(tv)) {
            // The whole height: the game's copy to the frame buffer then
            // scales its picture to it (the SDK sets that scale from the
            // table's heights).
            put16(p + kViY, 0);
            put16(p + kViHeight, full);
            put16(p + kXfbHeight, full);
            changed = true;
        }
        if (settings.deflicker != Deflicker::Game) {
            std::uint8_t before[7];
            for (std::size_t i = 0; i < 7; ++i) before[i] = p[kFilter + i];
            set_filter(p, settings.deflicker);
            for (std::size_t i = 0; i < 7; ++i) changed = changed || before[i] != p[kFilter + i];
        }
        if (changed) ++report.patched;
        at += kModeBytes - 4;
    }
}

}  // namespace riftwii
