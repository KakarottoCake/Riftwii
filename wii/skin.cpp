// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2009 Tantric (libwiigui template) <https://github.com/dborth/libwiigui>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.hpp"

#include <gccore.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "loadersettings.hpp"
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
GXColor kBar = {222, 222, 228, 255};
GXColor kDivider = {232, 232, 238, 255};
GXColor kScrollTrack = {230, 230, 236, 255};
GXColor kScrollThumb = {168, 168, 180, 255};
GXColor kBadge = {236, 236, 241, 255};
GXColor kShelfWood = {196, 160, 120, 255};
GXColor kShelfEdge = {150, 112, 76, 255};
GXColor kChipOn = {227, 245, 252, 255};
GXColor kChipOff = {244, 244, 246, 255};
GXColor kCard = {255, 255, 255, 255};
GXColor kChipOffEdge = {208, 208, 216, 255};

namespace {

bool g_ready = false;

GXColor g_backdrop = {236, 236, 239, 255};
GXColor g_backdrop_stripe = {227, 227, 232, 255};
bool g_stripes = true;
GXColor g_banner_tint = {0, 0, 0, 0};

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
    kShelfWood = ToGx(c.shelf);
    kShelfEdge = ToGx(c.shelf_edge);
    kChipOn = ToGx(c.chip_on);
    kChipOff = ToGx(c.chip_off);
    kCard = ToGx(c.card);
    kChipOffEdge = ToGx(c.chip_off_edge);
    g_backdrop = ToGx(c.backdrop);
    g_backdrop_stripe = ToGx(c.backdrop_stripe);
    g_stripes = t.stripes;
    g_banner_tint = ToGx(c.banner_tint);
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
    // Only a theme's own: without them the menu paints the backdrop
    // (GuiBackdrop) and mirrors the 640 bar outward.
    if (std::strcmp(im.name, "bar_wide") == 0) {
        std::vector<std::uint8_t> own;
        if (LoadThemeImage("bar", 640, 124, own)) return Tex();
    }
    if (std::strncmp(im.name, "background", 10) == 0) return Tex();
    Canvas c(0, 0);
    if (!paint_theme_image(im.name, MenuTheme(), c)) return Tex();
    return Upload(c);
}

}  // namespace

Tex background;
Tex backgroundWide, barWide;
Tex backgroundShelf, backgroundShelfWide;
Tex clockDigits;
Tex keys[4];
Tex card9;
Tex capsule9;
Tex homeBtnDanger, homeBtnDangerOver, iosClose, iosCloseOver, linen;
Tex backgroundPlain, backgroundPlainWide, backgroundChannels, backgroundChannelsWide;
static bool g_onHome = true;
Tex noticeIcon[2];
Tex tileEmpty;
Tex shelfPlank;

Tex ArtFrame(int w, int h) {
    // Two sizes at most (with and without the popup's hint line), each painted once.
    static Tex kept[2];
    for (Tex& t : kept)
        if (t.data && t.w == ((w + 2 * kHintBoxMargin + 3) & ~3) && t.h == ((h + 2 * kHintBoxMargin + 3) & ~3)) return t;
    for (Tex& t : kept)
        if (!t.data) {
            Canvas c(0, 0);
            paint_art_frame(MenuTheme(), w, h, c);
            t = Upload(c);
            return t;
        }
    return Tex();
}

Tex HintBox(int w, int h) {
    struct Kept {
        int w, h;
        Tex tex;
    };
    // A few at most (one per hover name and language): MEM2 is never given back.
    static std::vector<Kept> kept;
    for (const Kept& k : kept)
        if (k.w == w && k.h == h) return k.tex;
    if (kept.size() >= 12) return Tex();
    Canvas c(0, 0);
    paint_hint_box(MenuTheme(), w, h, c);
    kept.push_back(Kept{w, h, Upload(c)});
    return kept.back().tex;
}

void Init() {
    if (g_ready) return;
    LoadMenuTheme();
    ApplyColors(MenuTheme());
    struct Slot {
        const char* name;
        Tex* tex;
    };
    const Slot slots[] = {
        {"background", &background}, {"tile", &tile}, {"tile_over", &tileOver}, {"tile_empty", &tileEmpty}, {"cover_tile", &coverTile},
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
        {"pointer3", &hand[2]}, {"pointer4", &hand[3]}, {"shelf", &shelfPlank},
        {"background_wide", &backgroundWide}, {"bar_wide", &barWide},
        {"background_shelf", &backgroundShelf}, {"background_shelf_wide", &backgroundShelfWide},
        {"background_plain", &backgroundPlain}, {"background_plain_wide", &backgroundPlainWide},
        {"background_channels", &backgroundChannels}, {"background_channels_wide", &backgroundChannelsWide},
    };
    for (const ThemeImage& im : theme_images()) {
        for (const Slot& s : slots) {
            if (std::strcmp(s.name, im.name) == 0) *s.tex = Pick(im);
        }
    }
    {
        Canvas c(0, 0);
        paint_clock_digits(MenuTheme(), c);
        clockDigits = Upload(c);
        for (int k = 0; k < 4; ++k) {
            paint_key(MenuTheme(), k, c);
            keys[k] = Upload(c);
        }
        paint_card9(MenuTheme(), c);
        card9 = Upload(c);
        paint_capsule9(c);
        capsule9 = Upload(c);
        if (MenuTheme().home_ios6) {
            paint_home_danger(MenuTheme(), false, c);
            homeBtnDanger = Upload(c);
            paint_home_danger(MenuTheme(), true, c);
            homeBtnDangerOver = Upload(c);
            paint_ios_bar_button(MenuTheme(), false, c);
            iosClose = Upload(c);
            paint_ios_bar_button(MenuTheme(), true, c);
            iosCloseOver = Upload(c);
            paint_linen(c);
            linen = Upload(c);
        }
        for (int k = 0; k < 2; ++k) {
            paint_notice_icon(MenuTheme(), k == 1, c);
            noticeIcon[k] = Upload(c);
        }
    }
    g_ready = true;
}

bool BarBump() { return MenuTheme().bar_bump; }

bool HomeIos6() { return MenuTheme().home_ios6; }

void SetOnHome(bool home) { g_onHome = home; }

bool Ready() { return g_ready; }

GXColor HueFor(const std::string& id) {
    static const GXColor palette[] = {
        {179, 32, 47, 255},  {58, 45, 125, 255},  {184, 64, 28, 255}, {61, 90, 42, 255},  {176, 48, 111, 255},
        {39, 49, 63, 255},   {138, 90, 0, 255},   {31, 77, 107, 255}, {18, 110, 104, 255}, {110, 60, 140, 255},
    };
    std::uint32_t h = 2166136261u;
    for (char ch : id.substr(0, 4)) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
    const GXColor hue = palette[h % (sizeof(palette) / sizeof(palette[0]))];
    // The theme's tint, mixed in by its alpha (Bookshelf: warm wood).
    const unsigned a = g_banner_tint.a;
    const auto mix = [a](u8 own, u8 tint) { return static_cast<u8>((own * (255 - a) + tint * a + 127) / 255); };
    return {mix(hue.r, g_banner_tint.r), mix(hue.g, g_banner_tint.g), mix(hue.b, g_banner_tint.b), 255};
}

void Draw(const Tex& t, float x, float y, int alpha, float scale) {
    if (!t.data || alpha <= 0) return;
    Menu_DrawImg(x, y, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data, 0, scale, scale,
                 static_cast<u8>(alpha > 255 ? 255 : alpha));
}

void DrawNine(const Tex& t, float x, float y, float w, float h, float corner, int alpha) {
    if (!t.data || alpha <= 0) return;
    const float cx = std::min(corner, w / 2), cy = std::min(corner, h / 2);
    const float xs[4] = {x, x + cx, x + w - cx, x + w}, ys[4] = {y, y + cy, y + h - cy, y + h};
    const float us[4] = {0, corner / t.w, 1 - corner / t.w, 1}, vs[4] = {0, corner / t.h, 1 - corner / t.h, 1};
    const u8 a = static_cast<u8>(alpha > 255 ? 255 : alpha);
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k) {
            if (xs[k + 1] <= xs[k] || ys[r + 1] <= ys[r]) continue;
            Menu_DrawImgPart(xs[k], ys[r], xs[k + 1] - xs[k], ys[r + 1] - ys[r], static_cast<u16>(t.w), static_cast<u16>(t.h),
                             t.data, us[k], vs[r], us[k + 1], vs[r + 1], a);
        }
    }
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

bool WideMenu() {
    f32 x, w;
    Menu_SafeArea(&x, &w);
    return w > 700.0f;
}

namespace {

// Along one axis: screen [d0, d1) shows texels [s0, s1); s1 < s0 runs
// backwards, a mirror image.
struct Run {
    float d0, d1, s0, s1;
};

// A picture `size` texels long at `pos`, then mirror images of its `band`
// texels at each end, going outward back and forth; all cut to [lo, hi).
int Runs(float pos, int size, int band, float lo, float hi, Run* out, int max) {
    int n = 0;
    const auto add = [&](float d0, float d1, float s0, float s1) {
        const float c0 = d0 < lo ? lo : d0, c1 = d1 > hi ? hi : d1;
        if (c1 <= c0 || n == max) return;
        const float k = (s1 - s0) / (d1 - d0);
        out[n++] = Run{c0, c1, s0 + (c0 - d0) * k, s0 + (c1 - d0) * k};
    };
    if (band > size) band = size;
    for (int j = 0; band > 0 && pos - j * band > lo && n < max; ++j) {
        const float d1 = pos - j * band;
        if (j % 2 == 0) add(d1 - band, d1, band, 0);
        else add(d1 - band, d1, 0, band);
    }
    add(pos, pos + size, 0, size);
    for (int j = 0; band > 0 && pos + size + j * band < hi && n < max; ++j) {
        const float d0 = pos + size + j * band;
        if (j % 2 == 0) add(d0, d0 + band, size, size - band);
        else add(d0, d0 + band, size - band, size);
    }
    return n;
}

}  // namespace

void DrawExtended(const Tex& t, float x, float y, float left, float top, float right, float bottom, int bandX,
                  int bandY) {
    if (!t.data) return;
    constexpr int kMax = 12;
    Run xs[kMax], ys[kMax];
    const int nx = Runs(x, t.w, bandX, left, right, xs, kMax);
    const int ny = Runs(y, t.h, bandY, top, bottom, ys, kMax);
    const float w = static_cast<float>(t.w), h = static_cast<float>(t.h);
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            Menu_DrawImgPart(xs[i].d0, ys[j].d0, xs[i].d1 - xs[i].d0, ys[j].d1 - ys[j].d0, static_cast<u16>(t.w),
                             static_cast<u16>(t.h), t.data, xs[i].s0 / w, ys[j].s0 / h, xs[i].s1 / w, ys[j].s1 / h,
                             255);
        }
    }
}

void GuiBackdrop::Draw() {
    // The whole screen, also past the menu's 640x480 when it is drawn
    // smaller (widescreen, screen size). The theme's picture is never
    // stretched: it stays where the menu's rows are (Bookshelf paints
    // shelves under Home's rows of covers) and its edges are mirrored out
    // to the screen's. A widescreen menu takes the theme's wide picture
    // when it has one.
    // It stays put while a transition zooms or slides the screen on it.
    Menu_PushNoCamera();
    DrawBackdrop();
    Menu_PopCamera();
}

void GuiBackdrop::DrawBackdrop() {
    f32 vx, vy, vw, vh;
    Menu_VisibleArea(&vx, &vy, &vw, &vh);
    const bool wide = WideMenu();
    const Tex* pick = wide && backgroundWide.data ? &backgroundWide : &background;
    // The shelf view's own wall when the theme has one (Bookshelf: no
    // shelf through the boxes, where Home's upper row of covers stands).
    if (!g_onHome) {
        // Every other screen: the theme's plain wall when it has one
        // (Bookshelf: no shelves behind Settings' panels).
        if (wide && backgroundPlainWide.data) pick = &backgroundPlainWide;
        else if (backgroundPlain.data && !(wide && backgroundWide.data)) pick = &backgroundPlain;
    } else if (riftwii::wii::Settings().home_tiles == "shelf") {
        if (wide && backgroundShelfWide.data) pick = &backgroundShelfWide;
        else if (backgroundShelf.data && !(wide && backgroundWide.data)) pick = &backgroundShelf;
    } else if (riftwii::wii::Settings().home_tiles == "channels") {
        // Channels' own wall (Bookshelf: a shelf under each row of channels).
        if (wide && backgroundChannelsWide.data) pick = &backgroundChannelsWide;
        else if (backgroundChannels.data && !(wide && backgroundWide.data)) pick = &backgroundChannels;
    }
    const Tex& picture = *pick;
    if (picture.data) {
        DrawExtended(picture, 320.0f - picture.w / 2.0f, 0, vx, vy, vx + vw, vy + vh, picture.w, picture.h);
        return;
    }
    Menu_FillWholeScreen(g_backdrop);
    if (!g_stripes) return;
    for (int y = static_cast<int>(vy) / 4 * 4 + 2; y < vy + vh; y += 4) Menu_FillScreen(y, 2, g_backdrop_stripe);
}

}  // namespace riftwii::wii::skin
