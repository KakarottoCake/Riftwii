// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.hpp"

#include <gccore.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "menutheme.hpp"
#include "riftwii/canvas.hpp"
#include "riftwii/theme.hpp"

namespace riftwii::wii::skin {

Tex tile, tileOver, coverTile, coverTileOver, roundBtn, roundBtnOver, pill, pillOver, pillPrimary, pillPrimaryOver, homeBtn, homeBtnOver, chipOff, chipOn, rowFocus,
    stepBack, stepBackOver, stepForward, stepForwardOver, switchOn, switchOff,
    panelGame, panelSettings, bar, bannerStripes, arrowLeft, arrowLeftOver, arrowRight, arrowRightOver, iconDrives,
    scrollUp, scrollUpOver, scrollDown, scrollDownOver,
    iconGear, iconDisc, hand[4];

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

GXColor kInk = {46, 46, 54, 255};
GXColor kInkSoft = {74, 74, 84, 255};
GXColor kInkDim = {106, 106, 116, 255};
GXColor kClock = {116, 116, 126, 255};
GXColor kAccent = {47, 182, 233, 255};
GXColor kAccentInk = {14, 100, 136, 255};
GXColor kWarn = {176, 58, 46, 255};
GXColor kTextOnAccent = {255, 255, 255, 255};
GXColor kBar = {247, 247, 249, 255};
GXColor kDivider = {232, 232, 238, 255};
GXColor kScrollTrack = {230, 230, 236, 255};
GXColor kScrollThumb = {168, 168, 180, 255};
GXColor kBadge = {236, 236, 241, 255};

namespace {

bool g_ready = false;

// The painted pictures' colours, from the theme (ApplyColors).
Rgba kWhiteC = rgba(0xFFFFFF);  // the card colour
Rgba kEdge = rgba(0xCFCFD6);
Rgba kEdgeStrong = rgba(0xC4C4CE);
Rgba kShadow = rgba(0x28283C, 34);
Rgba kAccentC = rgba(0x2FB6E9);
Rgba kGlow = rgba(0x2FB6E9, 80);
Rgba kGlyph = rgba(0x55555F);
Rgba kChipOn = rgba(0xE3F5FC);
Rgba kChipOff = rgba(0xF4F4F6);
Rgba kChipOffEdge = rgba(0xD0D0D8);
Rgba kSwitchOff = rgba(0xD4D4DB);
Rgba kBarC = rgba(0xF7F7F9);
Rgba kBannerStripe = rgba(0xFFFFFF, 20);
GXColor g_backdrop = {236, 236, 239, 255};
GXColor g_backdrop_stripe = {227, 227, 232, 255};
bool g_stripes = true;
float g_corners = 1.0f;

Rgba ToRgba(const ThemeColor& c) { return Rgba{c.r, c.g, c.b, c.a}; }
GXColor ToGx(const ThemeColor& c) { return GXColor{c.r, c.g, c.b, c.a}; }

void ApplyColors(const Theme& t) {
    const ThemeColors& c = t.colors;
    kInk = ToGx(c.ink);
    kInkSoft = ToGx(c.ink_soft);
    kInkDim = ToGx(c.ink_dim);
    kClock = ToGx(c.clock);
    kAccent = ToGx(c.accent);
    kAccentInk = ToGx(c.accent_ink);
    kWarn = ToGx(c.warn);
    kTextOnAccent = ToGx(c.text_on_accent);
    kBar = ToGx(c.bar);
    kDivider = ToGx(c.divider);
    kScrollTrack = ToGx(c.scroll_track);
    kScrollThumb = ToGx(c.scroll_thumb);
    kBadge = ToGx(c.badge);
    kWhiteC = ToRgba(c.card);
    kEdge = ToRgba(c.card_edge);
    kEdgeStrong = ToRgba(c.card_edge_strong);
    kShadow = ToRgba(c.shadow);
    kAccentC = ToRgba(c.accent);
    kGlow = ToRgba(c.glow);
    kGlyph = ToRgba(c.glyph);
    kChipOn = ToRgba(c.chip_on);
    kChipOff = ToRgba(c.chip_off);
    kChipOffEdge = ToRgba(c.chip_off_edge);
    kSwitchOff = ToRgba(c.switch_off);
    kBarC = ToRgba(c.bar);
    kBannerStripe = ToRgba(c.banner_stripe);
    g_backdrop = ToGx(c.backdrop);
    g_backdrop_stripe = ToGx(c.backdrop_stripe);
    g_stripes = t.stripes;
    g_corners = t.corners;
}

// A corner radius as the theme rounds it.
float R(float radius) { return radius * g_corners; }

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
    // The card covers the shadows' insides: only their edges are worked out.
    radius = R(radius);
    if (over) c.shadow(x - 1, y - 1, w + 2.0f, h + 2.0f, radius + 1, margin - 1.0f, kGlow, 2.0f);
    c.shadow(x, y + 2, static_cast<float>(w), static_cast<float>(h), radius, 4, kShadow, 3.0f);
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
    c.rounded_rect(2, 3, 208, 30, R(15), on ? kChipOn : kChipOff);
    c.rounded_border(2, 3, 208, 30, R(15), 2, on ? kAccentC : kChipOffEdge);
    return Upload(c);
}

Tex Arrow(bool left, bool over) {
    Canvas c(48, 48);
    if (over) c.circle(24, 24, 20.5f, kGlow);
    c.circle(24, 25, 18.5f, kShadow);
    c.circle(24, 24, 18, kWhiteC);
    c.ring(24, 24, 18, 2, over ? kAccentC : kEdgeStrong);
    const float s = left ? -1.0f : 1.0f;
    c.line(24 - 3 * s, 17, 24 + 4 * s, 24, 3.5f, kGlyph);
    c.line(24 + 4 * s, 24, 24 - 3 * s, 31, 3.5f, kGlyph);
    return Upload(c);
}

// A list's scroll arrow: 34 across, in a 44 canvas, pointing up or down.
Tex ScrollArrow(bool up, bool over) {
    Canvas c(44, 44);
    if (over) c.circle(21, 21, 20.5f, kGlow);
    c.circle(21, 22.5f, 17.5f, kShadow);
    c.circle(21, 21, 17, over ? kChipOn : kWhiteC);
    c.ring(21, 21, 17, 2, over ? kAccentC : kEdgeStrong);
    const float s = up ? -1.0f : 1.0f;
    c.line(14, 21 - 2.5f * s, 21, 21 + 3.5f * s, 3.2f, over ? kAccentC : kGlyph);
    c.line(21, 21 + 3.5f * s, 28, 21 - 2.5f * s, 3.2f, over ? kAccentC : kGlyph);
    return Upload(c);
}

// A list row's arrow button: 34 across, in a 42 canvas.
Tex Step(bool back, bool over) {
    Canvas c(44, 44);
    if (over) c.circle(21, 21, 20.5f, kGlow);
    c.circle(21, 22.5f, 17.5f, kShadow);
    c.circle(21, 21, 17, over ? kChipOn : kWhiteC);
    c.ring(21, 21, 17, 2, over ? kAccentC : kEdgeStrong);
    const float s = back ? -1.0f : 1.0f;
    c.line(21 - 2.5f * s, 14, 21 + 3.5f * s, 21, 3.2f, over ? kAccentC : kGlyph);
    c.line(21 + 3.5f * s, 21, 21 - 2.5f * s, 28, 3.2f, over ? kAccentC : kGlyph);
    return Upload(c);
}

// An On/Off switch: 60x30 at (3, 4).
Tex Switch(bool on) {
    Canvas c(68, 40);
    c.rounded_rect(3, 4, 60, 30, R(15), on ? kAccentC : kSwitchOff);
    const float knob = on ? 48.0f : 18.0f;
    c.circle(knob, 20.5f, 12.5f, rgba(0x000000, 40));
    c.circle(knob, 19, 12, rgba(0xFFFFFF));
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

// A disc seen from above: silver, a track ring, the clear hub and its hole.
Tex Disc() {
    Canvas c(40, 40);
    c.circle(20, 20, 18.5f, rgba(0xDCDCE4));
    c.ring(20, 20, 18.5f, 1.6f, kGlyph);
    c.ring(20, 20, 12.5f, 1.0f, rgba(0xB4B4C0));
    c.circle(20, 20, 6.5f, rgba(0xFFFFFF));
    c.ring(20, 20, 6.5f, 1.4f, kGlyph);
    c.ring(20, 20, 2.6f, 1.2f, kGlyph);
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
    for (const Capsule& p : parts) c.line(p.x0, p.y0, p.x1, p.y1, p.w, rgba(0xFFFFFF));
    c.rounded_rect(38, 64, 33, 26, 9, rgba(0xFFFFFF));
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
    c.area_below(shade, rgba(0x000000, 18), 5.0f);  // the bar covers the rest
    c.area_below(top, kBarC);
    c.curve(top, 2.5f, kAccentC);
    return Upload(c);
}

Tex Stripes() {
    Canvas c(640, 192);
    c.diagonal_stripes(28, 12, kBannerStripe);
    return Upload(c);
}

Tex RowFocus() {
    Canvas c(548, 44);
    c.rounded_rect(1, 1, 546, 42, R(12), Rgba{kAccentC.r, kAccentC.g, kAccentC.b, 30});
    c.rounded_border(1, 1, 546, 42, R(12), 1.5f, Rgba{kAccentC.r, kAccentC.g, kAccentC.b, 150});
    return Upload(c);
}

// The theme's <name>.png, else what `paint` draws. The theme's picture
// must be the painted one's size (riftwii/theme.hpp lists them).
template <class Paint>
Tex Pick(const char* name, Paint paint) {
    for (const ThemeImage& im : theme_images()) {
        if (std::strcmp(im.name, name) != 0) continue;
        std::vector<std::uint8_t> rgba;
        if (!LoadThemeImage(name, im.w, im.h, rgba)) break;
        Tex t;
        t.data = Mem2Alloc(rgba.size());
        if (!t.data || !to_gx_rgba8(rgba.data(), im.w, im.h, t.data)) break;
        DCFlushRange(t.data, rgba.size());
        t.w = im.w;
        t.h = im.h;
        return t;
    }
    return paint();
}

}  // namespace

Tex background;

void Init() {
    if (g_ready) return;
    LoadMenuTheme();
    ApplyColors(MenuTheme());
    tile = Pick("tile", [] { return Card(124, 84, 7, 14, false); });
    tileOver = Pick("tile_over", [] { return Card(124, 84, 7, 14, true); });
    coverTile = Pick("cover_tile", [] { return Card(80, 112, 7, 8, false); });
    coverTileOver = Pick("cover_tile_over", [] { return Card(80, 112, 7, 8, true); });
    roundBtn = Pick("round_button", [] { return Round(false); });
    roundBtnOver = Pick("round_button_over", [] { return Round(true); });
    pill = Pick("pill", [] { return Card(244, 52, 4, 26, false); });
    pillOver = Pick("pill_over", [] { return Card(244, 52, 4, 26, true); });
    pillPrimary = Pick("pill_primary", [] { return Card(244, 52, 4, 26, false, true); });
    pillPrimaryOver = Pick("pill_primary_over", [] { return Card(244, 52, 4, 26, true, true); });
    homeBtn = Pick("home_button", [] { return Card(248, 72, 8, 20, false); });
    homeBtnOver = Pick("home_button_over", [] { return Card(248, 72, 8, 20, true); });
    chipOff = Pick("chip_off", [] { return Chip(false); });
    chipOn = Pick("chip_on", [] { return Chip(true); });
    rowFocus = Pick("row_focus", [] { return RowFocus(); });
    stepBack = Pick("step_back", [] { return Step(true, false); });
    stepBackOver = Pick("step_back_over", [] { return Step(true, true); });
    stepForward = Pick("step_forward", [] { return Step(false, false); });
    stepForwardOver = Pick("step_forward_over", [] { return Step(false, true); });
    switchOn = Pick("switch_on", [] { return Switch(true); });
    switchOff = Pick("switch_off", [] { return Switch(false); });
    panelGame = Pick("panel_game", [] { return Card(572, 232, 4, 16, false); });
    panelSettings = Pick("panel_settings", [] { return Card(572, 276, 4, 16, false); });
    bar = Pick("bar", [] { return Bar(); });
    bannerStripes = Pick("banner_stripes", [] { return Stripes(); });
    arrowLeft = Pick("arrow_left", [] { return Arrow(true, false); });
    arrowLeftOver = Pick("arrow_left_over", [] { return Arrow(true, true); });
    arrowRight = Pick("arrow_right", [] { return Arrow(false, false); });
    arrowRightOver = Pick("arrow_right_over", [] { return Arrow(false, true); });
    scrollUp = Pick("scroll_up", [] { return ScrollArrow(true, false); });
    scrollUpOver = Pick("scroll_up_over", [] { return ScrollArrow(true, true); });
    scrollDown = Pick("scroll_down", [] { return ScrollArrow(false, false); });
    scrollDownOver = Pick("scroll_down_over", [] { return ScrollArrow(false, true); });
    iconDrives = Pick("icon_drives", [] { return Drives(); });
    iconGear = Pick("icon_gear", [] { return Gear(); });
    iconDisc = Pick("icon_disc", [] { return Disc(); });
    const ThemeColors& c = MenuTheme().colors;
    const Rgba players[4] = {ToRgba(c.pointer1), ToRgba(c.pointer2), ToRgba(c.pointer3), ToRgba(c.pointer4)};
    static const char* const kPointers[4] = {"pointer1", "pointer2", "pointer3", "pointer4"};
    for (int i = 0; i < 4; ++i) hand[i] = Pick(kPointers[i], [&] { return Hand(players[i]); });
    background = Pick("background", [] { return Tex(); });
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

void DrawRgb5a3(const u8* data, int w, int h, float x, float y, int alpha, float scale) {
    if (!data || alpha <= 0) return;
    GXTexObj tex;
    GX_InitTexObj(&tex, const_cast<u8*>(data), static_cast<u16>(w), static_cast<u16>(h), GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP,
                  GX_FALSE);
    GX_LoadTexObj(&tex, GX_TEXMAP0);
    GX_InvalidateTexAll();
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    // Scaled about the centre, as Menu_DrawImg does, in libgui's 2D view
    // (the identity moved to z = -50, video.cpp).
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
    const f32 hw = w / 2.0f, hh = h / 2.0f;
    Mtx m, mv;
    guMtxIdentity(m);
    guMtxScaleApply(m, m, scale, scale, 1.0f);
    guMtxTransApply(m, m, x + hw, y + hh, 0);
    guMtxConcat(view, m, mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    const u8 a = static_cast<u8>(alpha > 255 ? 255 : alpha);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(-hw, -hh, 0);
    GX_Color4u8(0xFF, 0xFF, 0xFF, a);
    GX_TexCoord2f32(0, 0);
    GX_Position3f32(hw, -hh, 0);
    GX_Color4u8(0xFF, 0xFF, 0xFF, a);
    GX_TexCoord2f32(1, 0);
    GX_Position3f32(hw, hh, 0);
    GX_Color4u8(0xFF, 0xFF, 0xFF, a);
    GX_TexCoord2f32(1, 1);
    GX_Position3f32(-hw, hh, 0);
    GX_Color4u8(0xFF, 0xFF, 0xFF, a);
    GX_TexCoord2f32(0, 1);
    GX_End();
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
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
    if (background.data) {
        skin::Draw(background, 0, 0);
        return;
    }
    Menu_DrawRectangle(0, 0, screenwidth, screenheight, g_backdrop, 1);
    if (!g_stripes) return;
    for (int y = 2; y < screenheight; y += 4) Menu_DrawRectangle(0, y, screenwidth, 2, g_backdrop_stripe, 1);
}

}  // namespace riftwii::wii::skin
