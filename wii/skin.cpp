// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.hpp"

#include <gccore.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "riftwii/canvas.hpp"

namespace riftwii::wii::skin {

Tex tile, tileOver, roundBtn, roundBtnOver, pill, pillOver, pillPrimary, pillPrimaryOver, chipOff, chipOn, rowFocus,
    stepBack, stepBackOver, stepForward, stepForwardOver, switchOn, switchOff,
    panelGame, panelSettings, bar, bannerStripes, arrowLeft, arrowLeftOver, arrowRight, arrowRightOver, iconDrives,
    iconGear, hand[4];

// Textures and the menu font live below the MEM2 arena's low end, taken
// once and never freed: the menu keeps them until the game replaces all of
// memory, and the MEM1 heap stays for scans, packs and fragment lists.
u8* Mem2Alloc(std::size_t bytes) {
    u32 lo = (reinterpret_cast<u32>(SYS_GetArena2Lo()) + 31) & ~31u;
    const u32 end = lo + ((bytes + 31) & ~std::size_t(31));
    if (end > reinterpret_cast<u32>(SYS_GetArena2Hi())) return nullptr;
    SYS_SetArena2Lo(reinterpret_cast<void*>(end));
    return reinterpret_cast<u8*>(lo);
}

namespace {

bool g_ready = false;

const Rgba kWhiteC = rgba(0xFFFFFF);
const Rgba kEdge = rgba(0xCFCFD6);
const Rgba kEdgeStrong = rgba(0xC4C4CE);
const Rgba kShadow = rgba(0x28283C, 34);
const Rgba kAccentC = rgba(0x2FB6E9);
const Rgba kGlow = rgba(0x2FB6E9, 80);
const Rgba kGlyph = rgba(0x55555F);

Tex Upload(const Canvas& c) {
    Tex t;
    const std::vector<std::uint8_t> gx = to_gx_rgba8(c);
    if (gx.empty()) return t;
    t.data = Mem2Alloc(gx.size());
    if (!t.data) return t;
    std::memcpy(t.data, gx.data(), gx.size());
    DCFlushRange(t.data, gx.size());
    t.w = c.width();
    t.h = c.height();
    return t;
}

// A card: shadow, white body, border; `over` adds the accent outline and glow.
Tex Card(int w, int h, int margin, float radius, bool over, bool primary = false) {
    // GX textures come in 4x4 tiles: pad right and bottom to fit.
    Canvas c((w + 2 * margin + 3) & ~3, (h + 2 * margin + 3) & ~3);
    const float x = static_cast<float>(margin), y = static_cast<float>(margin);
    if (over) c.shadow(x - 1, y - 1, w + 2.0f, h + 2.0f, radius + 1, margin - 1.0f, kGlow);
    c.shadow(x, y + 2, static_cast<float>(w), static_cast<float>(h), radius, 4, kShadow);
    c.rounded_rect(x, y, static_cast<float>(w), static_cast<float>(h), radius, kWhiteC);
    if (over || primary) {
        c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, primary ? 3.0f : 2.5f, kAccentC);
    } else {
        c.rounded_border(x, y, static_cast<float>(w), static_cast<float>(h), radius, 1.5f, kEdge);
    }
    return Upload(c);
}

Tex Round(bool over) {
    Canvas c(80, 80);
    if (over) c.circle(40, 40, 39, kGlow);
    c.circle(40, 41.5f, 38, kShadow);
    c.circle(40, 40, 37, kWhiteC);
    c.ring(40, 40, 37, over ? 2.5f : 2.0f, over ? kAccentC : kEdgeStrong);
    return Upload(c);
}

Tex Chip(bool on) {
    Canvas c(212, 36);
    c.rounded_rect(2, 3, 208, 30, 15, on ? rgba(0xE3F5FC) : rgba(0xF4F4F6));
    c.rounded_border(2, 3, 208, 30, 15, 2, on ? kAccentC : rgba(0xD0D0D8));
    return Upload(c);
}

Tex Arrow(bool left, bool over) {
    Canvas c(48, 48);
    if (over) c.circle(24, 24, 23, kGlow);
    c.circle(24, 25, 21, kShadow);
    c.circle(24, 24, 20, kWhiteC);
    c.ring(24, 24, 20, 2, over ? kAccentC : kEdgeStrong);
    const float s = left ? -1.0f : 1.0f;
    c.line(24 - 3 * s, 16, 24 + 4 * s, 24, 3.5f, kGlyph);
    c.line(24 + 4 * s, 24, 24 - 3 * s, 32, 3.5f, kGlyph);
    return Upload(c);
}

// A list row's arrow button: 34 across, in a 42 canvas.
Tex Step(bool back, bool over) {
    Canvas c(44, 44);
    if (over) c.circle(21, 21, 20.5f, kGlow);
    c.circle(21, 22.5f, 17.5f, kShadow);
    c.circle(21, 21, 17, over ? rgba(0xE3F5FC) : kWhiteC);
    c.ring(21, 21, 17, 2, over ? kAccentC : kEdgeStrong);
    const float s = back ? -1.0f : 1.0f;
    c.line(21 - 2.5f * s, 14, 21 + 3.5f * s, 21, 3.2f, over ? kAccentC : kGlyph);
    c.line(21 + 3.5f * s, 21, 21 - 2.5f * s, 28, 3.2f, over ? kAccentC : kGlyph);
    return Upload(c);
}

// An On/Off switch: 60x30 at (3, 4).
Tex Switch(bool on) {
    Canvas c(68, 40);
    c.rounded_rect(3, 4, 60, 30, 15, on ? kAccentC : rgba(0xD4D4DB));
    const float knob = on ? 48.0f : 18.0f;
    c.circle(knob, 20.5f, 12.5f, rgba(0x000000, 40));
    c.circle(knob, 19, 12, kWhiteC);
    return Upload(c);
}

Tex Drives() {
    Canvas c(28, 28);
    c.rounded_border(3, 4, 22, 8, 2.5f, 1.8f, kGlyph);
    c.rounded_border(3, 16, 22, 8, 2.5f, 1.8f, kGlyph);
    c.circle(8, 8, 1.3f, kGlyph);
    c.circle(8, 20, 1.3f, kGlyph);
    return Upload(c);
}

Tex Gear() {
    Canvas c(28, 28);
    for (int i = 0; i < 8; ++i) {
        const float a = i * 3.14159265f / 4.0f;
        c.line(14 + 7.5f * std::cos(a), 14 + 7.5f * std::sin(a), 14 + 11.0f * std::cos(a), 14 + 11.0f * std::sin(a), 4.0f,
               kGlyph);
    }
    c.ring(14, 14, 8.5f, 3.0f, kGlyph);
    c.ring(14, 14, 3.5f, 1.8f, kGlyph);
    return Upload(c);
}

// A white pointing hand with the player's colour as its outline; the
// fingertip is the texture's centre, so the Wii Remote's roll turns the
// hand about the point it aims at.
Tex Hand(Rgba outline) {
    Canvas c(96, 96);
    struct Capsule { float x0, y0, x1, y1, w; };
    const Capsule parts[] = {
        {48, 54, 48, 70, 12},   // index finger
        {59, 67, 59, 75, 10},   // knuckles
        {67, 70, 67, 77, 9},
        {41, 79, 33, 71, 11},   // thumb
    };
    const auto shadow = rgba(0x000000, 60);
    for (const Capsule& p : parts) c.line(p.x0 + 1.5f, p.y0 + 2.5f, p.x1 + 1.5f, p.y1 + 2.5f, p.w + 4, shadow);
    c.rounded_rect(39.5f, 65.5f, 37, 30, 11, shadow);
    for (const Capsule& p : parts) c.line(p.x0, p.y0, p.x1, p.y1, p.w + 4, outline);
    c.rounded_rect(36, 62, 37, 30, 11, outline);
    for (const Capsule& p : parts) c.line(p.x0, p.y0, p.x1, p.y1, p.w, kWhiteC);
    c.rounded_rect(38, 64, 33, 26, 9, kWhiteC);
    return Upload(c);
}

Tex Bar() {
    Canvas c(640, 124);
    std::vector<float> top(640);
    for (int x = 0; x < 640; ++x) {
        float t = (x + 0.5f - 176.0f) / 288.0f;
        const float bump = (t > 0.0f && t < 1.0f) ? (1.0f - std::cos(t * 2.0f * 3.14159265f)) * 0.5f : 0.0f;
        top[x] = 46.0f - 32.0f * bump;
    }
    std::vector<float> shade(top);
    for (float& v : shade) v -= 3.0f;
    c.area_below(shade, rgba(0x000000, 18));
    c.area_below(top, rgba(0xF7F7F9));
    c.curve(top, 2.5f, kAccentC);
    return Upload(c);
}

Tex Stripes() {
    Canvas c(640, 192);
    for (int k = -200; k < 840; k += 28) c.line(static_cast<float>(k), 200, k + 200.0f, 0, 12, rgba(0xFFFFFF, 20));
    return Upload(c);
}

}  // namespace

void Init() {
    if (g_ready) return;
    tile = Card(134, 84, 7, 14, false);
    tileOver = Card(134, 84, 7, 14, true);
    roundBtn = Round(false);
    roundBtnOver = Round(true);
    pill = Card(244, 52, 4, 26, false);
    pillOver = Card(244, 52, 4, 26, true);
    pillPrimary = Card(244, 52, 4, 26, false, true);
    pillPrimaryOver = Card(244, 52, 4, 26, true, true);
    chipOff = Chip(false);
    chipOn = Chip(true);
    {
        Canvas c(548, 44);
        c.rounded_rect(1, 1, 546, 42, 12, rgba(0x2FB6E9, 30));
        c.rounded_border(1, 1, 546, 42, 12, 1.5f, rgba(0x2FB6E9, 150));
        rowFocus = Upload(c);
    }
    stepBack = Step(true, false);
    stepBackOver = Step(true, true);
    stepForward = Step(false, false);
    stepForwardOver = Step(false, true);
    switchOn = Switch(true);
    switchOff = Switch(false);
    panelGame = Card(572, 232, 4, 16, false);
    panelSettings = Card(572, 276, 4, 16, false);
    bar = Bar();
    bannerStripes = Stripes();
    arrowLeft = Arrow(true, false);
    arrowLeftOver = Arrow(true, true);
    arrowRight = Arrow(false, false);
    arrowRightOver = Arrow(false, true);
    iconDrives = Drives();
    iconGear = Gear();
    const Rgba players[4] = {rgba(0x3B8FD6), rgba(0xD64545), rgba(0x3FA34D), rgba(0xD9A21B)};
    for (int i = 0; i < 4; ++i) hand[i] = Hand(players[i]);
    g_ready = true;
}

bool Ready() { return g_ready; }

GXColor HueFor(const std::string& id) {
    static const GXColor palette[] = {
        {179, 32, 47, 255},  {58, 45, 125, 255},  {184, 64, 28, 255}, {61, 90, 42, 255},  {176, 48, 111, 255},
        {39, 49, 63, 255},   {138, 90, 0, 255},   {31, 77, 107, 255}, {18, 110, 104, 255}, {110, 60, 140, 255},
    };
    std::uint32_t h = 2166136261u;
    for (char ch : id.substr(0, 4)) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
    return palette[h % (sizeof(palette) / sizeof(palette[0]))];
}

void Draw(const Tex& t, float x, float y, int alpha, float scale) {
    if (!t.data || alpha <= 0) return;
    Menu_DrawImg(x, y, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data, 0, scale, scale,
                 static_cast<u8>(alpha > 255 ? 255 : alpha));
}

GXColor WithAlpha(GXColor c, int alpha) {
    c.a = static_cast<u8>(c.a * (alpha < 0 ? 0 : alpha > 255 ? 255 : alpha) / 255);
    return c;
}

GuiBackdrop::GuiBackdrop() {
    width = screenwidth;
    height = screenheight;
}

void GuiBackdrop::Draw() {
    Menu_DrawRectangle(0, 0, screenwidth, screenheight, (GXColor){236, 236, 239, 255}, 1);
    for (int y = 2; y < screenheight; y += 4) Menu_DrawRectangle(0, y, screenwidth, 2, (GXColor){227, 227, 232, 255}, 1);
}

}  // namespace riftwii::wii::skin
