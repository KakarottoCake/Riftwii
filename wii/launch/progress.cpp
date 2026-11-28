// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "progress.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <wiiuse/wpad.h>

#include <cstdio>
#include <cstring>

#include "console.hpp"
#include "guiscript.hpp"
#include "ios_reload.hpp"

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

// The menu's colours (wii/menu/skin.hpp): the card, its ink, the accent.
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

// The console font's code (code page 437) for a Latin-1 letter, 0 for
// none. Letters it lacks (Portuguese ã and õ, most capitals with an
// accent) are drawn as their plain letter: still readable.
u8 Cp437(unsigned code) {
    switch (code) {
    case 0xC7: return 0x80;  // Ç
    case 0xFC: return 0x81;  // ü
    case 0xE9: return 0x82;  // é
    case 0xE2: return 0x83;  // â
    case 0xE4: return 0x84;  // ä
    case 0xE0: return 0x85;  // à
    case 0xE5: return 0x86;  // å
    case 0xE7: return 0x87;  // ç
    case 0xEA: return 0x88;  // ê
    case 0xEB: return 0x89;  // ë
    case 0xE8: return 0x8A;  // è
    case 0xEF: return 0x8B;  // ï
    case 0xEE: return 0x8C;  // î
    case 0xEC: return 0x8D;  // ì
    case 0xC4: return 0x8E;  // Ä
    case 0xC9: return 0x90;  // É
    case 0xF4: return 0x93;  // ô
    case 0xF6: return 0x94;  // ö
    case 0xF2: return 0x95;  // ò
    case 0xFB: return 0x96;  // û
    case 0xF9: return 0x97;  // ù
    case 0xD6: return 0x99;  // Ö
    case 0xDC: return 0x9A;  // Ü
    case 0xE1: return 0xA0;  // á
    case 0xED: return 0xA1;  // í
    case 0xF3: return 0xA2;  // ó
    case 0xFA: return 0xA3;  // ú
    case 0xF1: return 0xA4;  // ñ
    case 0xD1: return 0xA5;  // Ñ
    case 0xBF: return 0xA8;  // ¿
    case 0xA1: return 0xAD;  // ¡
    case 0xAB: return 0xAE;  // «
    case 0xBB: return 0xAF;  // »
    case 0xE3: return 'a';   // ã
    case 0xF5: return 'o';   // õ
    case 0xC0: case 0xC1: case 0xC2: case 0xC3: return 'A';
    case 0xC8: case 0xCA: case 0xCB: return 'E';
    case 0xCC: case 0xCD: case 0xCE: case 0xCF: return 'I';
    case 0xD2: case 0xD3: case 0xD4: case 0xD5: return 'O';
    case 0xD9: case 0xDA: case 0xDB: return 'U';
    default: return 0;
    }
}

// UTF-8 `text` as the console font's codes into `out` (`size` bytes, cut
// to fit, always terminated). False when it has a letter the font can't
// draw (Japanese, Korean) or isn't UTF-8: the caller draws another text.
bool ToConsoleFont(const char* text, char* out, std::size_t size) {
    std::size_t n = 0;
    for (const u8* p = reinterpret_cast<const u8*>(text); *p != 0;) {
        unsigned code = 0;
        if (*p < 0x80) {
            code = *p++;
        } else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) {
            code = ((p[0] & 0x1Fu) << 6) | (p[1] & 0x3Fu);
            p += 2;
        } else {
            return false;  // three bytes or more: beyond Latin-1
        }
        const u8 glyph = code < 0x80 ? static_cast<u8>(code) : Cp437(code);
        if (glyph == 0) return false;
        if (n + 1 < size) out[n++] = static_cast<char>(glyph);
    }
    out[n] = '\0';
    return true;
}

// With the game's box on the card instead of the log: a button pressed
// on any pad shows the log. Looked at every 50 ms at most, as the bar's
// calls come in (in a read loop, thousands of times). The Wii's own
// GameCube ports at any time (SI has nothing to do with the IOS; the
// USB adapter was stopped as the menu closed); the Wii Remotes only
// until their stack is shut down before an IOS reload, so a player
// with only a Wii Remote can ask during the first part of a launch
// only. A failure shows the log anyway (WaitForChoice).
u64 g_last_look = 0;

void ShowLogOnButton() {
    if (!g_xfb || !ConsoleDeferred()) return;
    const u64 now = gettime();
    if (g_last_look != 0 && ticks_to_millisecs(now - g_last_look) < 50) return;
    g_last_look = now;
    bool pressed = false;
    PAD_ScanPads();
    for (int i = 0; i < 4; ++i) {
        if (PAD_ButtonsDown(i) != 0) pressed = true;
    }
    if (!wii_remotes_released()) {
        WPAD_ScanPads();
        for (int i = 0; i < 4; ++i) {
            if (WPAD_ButtonsDown(i) != 0) pressed = true;
        }
    }
    if (!pressed) return;
    ConsoleRevealDeferred();
    Redraw();
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
    ShowLogOnButton();
    // Test screenshots, only while the card is certainly up.
    if (g_xfb && g_percent <= 60) GuiScriptLaunchShot(g_percent, g_xfb, g_stride * 2, g_screen_height);
}

void ProgressNote(const char* note, const char* english) {
    if (!g_xfb || !ConsoleDeferred()) return;
    // The row above the label, inside the card's white under the box
    // (the box ends well above it, wii/menu/rift_menu.cpp ShowLaunchFrame).
    constexpr int kNoteGap = 4;
    const int y = g_y - kLabelHeight - kNoteGap;
    char line[96];
    const std::size_t room = static_cast<std::size_t>(g_width / 8) + 1;  // with the terminator
    const std::size_t size = room < sizeof(line) ? room : sizeof(line);
    if (!ToConsoleFont(note, line, size)) ToConsoleFont(english, line, size);
    Fill(g_x, y, g_width, kLabelHeight, kPaper);
    Text(g_x, y, line);
    DCFlushRange(g_xfb + y * g_stride, kLabelHeight * g_stride * 4);
}

void ProgressWithin(unsigned long long done, unsigned long long total, int from, int to) {
    ShowLogOnButton();
    if (total == 0) return;
    if (done > total) done = total;
    const int percent = from + static_cast<int>((to - from) * done / total);
    if (percent <= g_percent) return;
    g_percent = percent;
    Redraw();
}

}  // namespace riftwii::wii
