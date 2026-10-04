// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.hpp"

#include <gccore.h>

#include <cstring>
#include <vector>

#include "menutheme.hpp"
#include "riftwii/canvas.hpp"
#include "riftwii/skinpaint.hpp"
#include "riftwii/theme.hpp"

namespace riftwii::wii::skin {

Tex tile, tileOver, coverTile, coverTileOver, roundBtn, roundBtnOver, pill, pillOver, pillPrimary, pillPrimaryOver, homeBtn, homeBtnOver, chipOff, chipOn, rowFocus,
    stepBack, stepBackOver, stepForward, stepForwardOver, switchOn, switchOff,
    panelGame, panelSettings, bar, bannerStripes, arrowLeft, arrowLeftOver, arrowRight, arrowRightOver, iconDrives,
    scrollUp, scrollUpOver, scrollDown, scrollDownOver,
    iconGear, iconDisc, iconSearch, hand[4];

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
GXColor kChipOn = {227, 245, 252, 255};
GXColor kChipOff = {244, 244, 246, 255};
GXColor kChipOffEdge = {208, 208, 216, 255};

namespace {

bool g_ready = false;

GXColor g_backdrop = {236, 236, 239, 255};
GXColor g_backdrop_stripe = {227, 227, 232, 255};
bool g_stripes = true;

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
    kChipOn = ToGx(c.chip_on);
    kChipOff = ToGx(c.chip_off);
    kChipOffEdge = ToGx(c.chip_off_edge);
    g_backdrop = ToGx(c.backdrop);
    g_backdrop_stripe = ToGx(c.backdrop_stripe);
    g_stripes = t.stripes;
}

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

// The theme's <name>.png, else RiftWii's own picture in the theme's
// colours (src/skinpaint.cpp). The theme's picture must be the painted
// one's size (riftwii/theme.hpp lists them). The backdrop has no picture
// of its own: GuiBackdrop draws it, unless the theme gives background.png.
Tex Pick(const ThemeImage& im) {
    std::vector<std::uint8_t> rgba;
    if (LoadThemeImage(im.name, im.w, im.h, rgba)) {
        Tex t;
        t.data = Mem2Alloc(rgba.size());
        if (t.data && to_gx_rgba8(rgba.data(), im.w, im.h, t.data)) {
            DCFlushRange(t.data, rgba.size());
            t.w = im.w;
            t.h = im.h;
            return t;
        }
    }
    if (std::strcmp(im.name, "background") == 0) return Tex();
    Canvas c(0, 0);
    if (!paint_theme_image(im.name, MenuTheme(), c)) return Tex();
    return Upload(c);
}

}  // namespace

Tex background;

void Init() {
    if (g_ready) return;
    LoadMenuTheme();
    ApplyColors(MenuTheme());
    struct Slot {
        const char* name;
        Tex* tex;
    };
    const Slot slots[] = {
        {"background", &background}, {"tile", &tile}, {"tile_over", &tileOver}, {"cover_tile", &coverTile},
        {"cover_tile_over", &coverTileOver}, {"round_button", &roundBtn}, {"round_button_over", &roundBtnOver},
        {"pill", &pill}, {"pill_over", &pillOver}, {"pill_primary", &pillPrimary},
        {"pill_primary_over", &pillPrimaryOver}, {"home_button", &homeBtn}, {"home_button_over", &homeBtnOver},
        {"chip_off", &chipOff}, {"chip_on", &chipOn}, {"row_focus", &rowFocus}, {"step_back", &stepBack},
        {"step_back_over", &stepBackOver}, {"step_forward", &stepForward}, {"step_forward_over", &stepForwardOver},
        {"switch_on", &switchOn}, {"switch_off", &switchOff}, {"panel_game", &panelGame},
        {"panel_settings", &panelSettings}, {"bar", &bar}, {"banner_stripes", &bannerStripes},
        {"arrow_left", &arrowLeft}, {"arrow_left_over", &arrowLeftOver}, {"arrow_right", &arrowRight},
        {"arrow_right_over", &arrowRightOver}, {"scroll_up", &scrollUp}, {"scroll_up_over", &scrollUpOver},
        {"scroll_down", &scrollDown}, {"scroll_down_over", &scrollDownOver}, {"icon_drives", &iconDrives},
        {"icon_gear", &iconGear}, {"icon_search", &iconSearch}, {"icon_disc", &iconDisc}, {"pointer1", &hand[0]}, {"pointer2", &hand[1]},
        {"pointer3", &hand[2]}, {"pointer4", &hand[3]},
    };
    for (const ThemeImage& im : theme_images()) {
        for (const Slot& s : slots) {
            if (std::strcmp(s.name, im.name) == 0) *s.tex = Pick(im);
        }
    }
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
