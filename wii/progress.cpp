// SPDX-License-Identifier: GPL-3.0-or-later
#include "progress.hpp"

#include <gccore.h>

#include <cstdio>
#include <cstring>

#include "guiscript.hpp"

extern "C" u8 console_font_8x16[];

namespace riftwii::wii {
namespace {

struct Yuv {
    u8 y, u, v;
};

// BT.601, studio range, as the VI expects.
constexpr Yuv ToYuv(int r, int g, int b) {
    return Yuv{static_cast<u8>((66 * r + 129 * g + 25 * b + 128) / 256 + 16),
               static_cast<u8>((-38 * r - 74 * g + 112 * b + 128) / 256 + 128),
               static_cast<u8>((112 * r - 94 * g - 18 * b + 128) / 256 + 128)};
}

// The menu's colours (wii/skin.hpp): the card, its ink, the accent.
constexpr Yuv kPaper = ToYuv(255, 255, 255);
constexpr Yuv kInk = ToYuv(46, 46, 54);
constexpr Yuv kTrack = ToYuv(226, 226, 232);
constexpr Yuv kFill = ToYuv(47, 182, 233);

constexpr int kLabelHeight = 16;
constexpr int kBarTop = 22;
constexpr int kBarHeight = 10;

u32* g_xfb = nullptr;
int g_stride = 0;  // in 32-bit words (two pixels each)
int g_x = 0, g_y = 0, g_width = 0;
int g_percent = 0;
int g_screen_height = 0;
char g_stage[64] = "";

// Fills whole pixel pairs: x and width are rounded to even.
void Fill(int x, int y, int width, int height, Yuv c) {
    const u32 word = (u32(c.y) << 24) | (u32(c.u) << 16) | (u32(c.y) << 8) | c.v;
    for (int row = y; row < y + height; ++row) {
        u32* p = g_xfb + row * g_stride + x / 2;
        for (int i = 0; i < width / 2; ++i) p[i] = word;
    }
}

// 8x16 console glyphs, ink on paper; a pair's chroma is the paper's (both
// colours are grey enough for that not to show).
void Text(int x, int y, const char* s) {
    for (; *s != '\0'; ++s, x += 8) {
        const u8* glyph = console_font_8x16 + static_cast<u8>(*s) * 16;
        for (int row = 0; row < 16; ++row) {
            u32* p = g_xfb + (y + row) * g_stride + x / 2;
            for (int pair = 0; pair < 4; ++pair) {
                const bool left = glyph[row] & (0x80 >> (pair * 2));
                const bool right = glyph[row] & (0x40 >> (pair * 2));
                p[pair] = (u32(left ? kInk.y : kPaper.y) << 24) | (u32(kPaper.u) << 16) |
                          (u32(right ? kInk.y : kPaper.y) << 8) | kPaper.v;
            }
        }
    }
}

void Redraw() {
    if (!g_xfb) return;
    Fill(g_x, g_y, g_width, kLabelHeight, kPaper);
    char label[96];
    std::snprintf(label, sizeof(label), "%s...", g_stage);
    Text(g_x, g_y, label);
    char percent[8];
    std::snprintf(percent, sizeof(percent), "%d%%", g_percent);
    Text(g_x + g_width - 8 * static_cast<int>(std::strlen(percent)), g_y, percent);
    const int filled = (g_width * g_percent / 100) & ~1;
    Fill(g_x, g_y + kBarTop, filled, kBarHeight, kFill);
    Fill(g_x + filled, g_y + kBarTop, g_width - filled, kBarHeight, kTrack);
    // The menu's framebuffers are cached: the VI reads memory.
    DCFlushRange(g_xfb + g_y * g_stride, (kBarTop + kBarHeight) * g_stride * 4);
}

}  // namespace

void ProgressAttach(void* xfb, int fb_width, int fb_height, int x, int y, int width) {
    g_screen_height = fb_height;
    g_xfb = static_cast<u32*>(xfb);
    g_stride = fb_width / 2;
    g_x = x & ~1;
    g_y = y;
    g_width = width & ~1;
    g_percent = 0;
    std::strcpy(g_stage, "Getting ready");
    Redraw();
}

void ProgressStage(const char* stage, int percent) {
    std::snprintf(g_stage, sizeof(g_stage), "%s", stage);
    if (percent > g_percent) g_percent = percent > 100 ? 100 : percent;
    Redraw();
    // Test screenshots, only while the card is certainly up.
    if (g_xfb && g_percent <= 60) GuiScriptLaunchShot(g_percent, g_xfb, g_stride * 2, g_screen_height);
}

void ProgressWithin(unsigned long long done, unsigned long long total, int from, int to) {
    if (total == 0) return;
    if (done > total) done = total;
    const int percent = from + static_cast<int>((to - from) * done / total);
    if (percent <= g_percent) return;
    g_percent = percent;
    Redraw();
}

}  // namespace riftwii::wii
