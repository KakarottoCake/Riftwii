// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-FileCopyrightText: 2009 Tantric (libwiigui template) <https://github.com/dborth/libwiigui>
// SPDX-License-Identifier: GPL-3.0-or-later
#include "skin.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>

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
    iconGear, iconDisc, discCase, iconSearch, iconStar, iconStarOn, hand[4];

// Textures and the menu font live below the MEM2 arena's low end, taken
// once and never freed: the menu keeps them until the game replaces all of
// memory, and the MEM1 heap stays for scans, packs and fragment lists.
// The menu thread and the GUI thread both take memory here (the USB drive
// pick against box art and cover slots), so reading the arena's low end
// and moving it is one step with interrupts off: two callers could
// otherwise be handed the same block.
u8* Mem2Alloc(std::size_t bytes) {
    u32 level;
    _CPU_ISR_Disable(level);
    const u32 lo = (reinterpret_cast<u32>(SYS_GetArena2Lo()) + 31) & ~31u;
    const u32 size = (bytes + 31) & ~std::size_t(31);
    const u32 hi = reinterpret_cast<u32>(SYS_GetArena2Hi());
    u8* out = nullptr;
    if (lo <= hi && size <= hi - lo) {
        SYS_SetArena2Lo(reinterpret_cast<void*>(lo + size));
        out = reinterpret_cast<u8*>(lo);
    }
    _CPU_ISR_Restore(level);
    return out;
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

// The clock-dial theme's soyjak (themes/TheTime/soyjak.png, by
// soyjak.party, used with their blessing), at the wall's right edge,
// a quarter bigger, his feet behind the bar.
void AddSoyjak(Canvas& c) {
    constexpr int kW = 192, kH = 304;
    std::vector<std::uint8_t> px;
    if (!LoadThemeImage("soyjak", kW, kH, px)) return;
    const float scale = 1.25f;
    const int dw = static_cast<int>(kW * scale), dh = static_cast<int>(kH * scale);
    // Leaning in from the right edge, clear of the shelf's boxes.
    const int ox = c.width() - dw + 96, oy = 480 - dh + 6;
    for (int y = 0; y < dh; ++y) {
        const int ty = oy + y;
        if (ty < 0 || ty >= c.height()) continue;
        const int sy = std::min(kH - 1, static_cast<int>(y / scale));
        for (int x = 0; x < dw; ++x) {
            const int tx = ox + x;
            if (tx < 0 || tx >= c.width()) continue;
            const std::uint8_t* s = &px[(static_cast<std::size_t>(sy) * kW + std::min(kW - 1, static_cast<int>(x / scale))) * 4];
            const int a = s[3];
            if (a == 0) continue;
            const Rgba d = c.at(tx, ty);
            c.put(tx, ty, Rgba{static_cast<std::uint8_t>((s[0] * a + d.r * (255 - a)) / 255),
                               static_cast<std::uint8_t>((s[1] * a + d.g * (255 - a)) / 255),
                               static_cast<std::uint8_t>((s[2] * a + d.b * (255 - a)) / 255), 255});
        }
    }
}

// The theme's <name>.png, else RiftWii's own picture in the theme's
// colours (src/art/skinpaint.cpp). The theme's picture must be the painted
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
    // The clock-dial theme paints its own (only the two: the others fall back to them).
    if (std::strncmp(im.name, "background", 10) == 0 &&
        !(MenuTheme().clock_dials && (std::strcmp(im.name, "background") == 0 || std::strcmp(im.name, "background_wide") == 0)))
        return Tex();
    Canvas c(0, 0);
    if (!paint_theme_image(im.name, MenuTheme(), c)) return Tex();
    if (MenuTheme().clock_dials && std::strncmp(im.name, "background", 10) == 0) AddSoyjak(c);
    return Upload(c);
}

}  // namespace

Tex background;
Tex backgroundWide, barWide;
Tex backgroundShelf, backgroundShelfWide;
Tex clockDigits;
Tex keys[4];
Tex card9;
Tex dropShadow9;
Tex channelFrame9, channelFrameOver9;
Tex label9;
Tex capsule9;
Tex homeBtnDanger, homeBtnDangerOver, iosClose, iosCloseOver, linen;
Tex backgroundPlain, backgroundPlainWide, backgroundChannels, backgroundChannelsWide;
static bool g_onHome = true;
// Home's view for its wall, set by the menu thread: the GUI thread draws
// the backdrop every frame and must not read Settings()' strings while
// the menu thread may be changing them.
// Home's wall: the covers' (Plain), the shelf view's, the channels' (and names'), or
// none of their rows (Bare: the theme's plain wall, as on other screens).
enum class HomeWall { Plain, Shelf, Channels, Bare };
static volatile HomeWall g_homeWall = HomeWall::Plain;
Tex noticeIcon[2];
Tex tileEmpty;
Tex shelfPlank;
Tex tile9, tile9Over, tile9Primary, tile9PrimaryOver, promptButton;
Tex iconPlay, iconMods, iconCheats, iconSaves, iconInfo, iconHelp, iconPicture, iconControllers, iconOnline,
    iconHome, iconSound, iconPower;

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
        {"icon_gear", &iconGear}, {"icon_search", &iconSearch}, {"icon_star", &iconStar}, {"icon_star_on", &iconStarOn}, {"icon_disc", &iconDisc}, {"disc_case", &discCase}, {"pointer1", &hand[0]}, {"pointer2", &hand[1]},
        {"pointer3", &hand[2]}, {"pointer4", &hand[3]}, {"shelf", &shelfPlank},
        {"tile9", &tile9}, {"tile9_over", &tile9Over}, {"tile9_primary", &tile9Primary},
        {"tile9_primary_over", &tile9PrimaryOver}, {"prompt_button", &promptButton}, {"icon_play", &iconPlay},
        {"icon_mods", &iconMods}, {"icon_cheats", &iconCheats}, {"icon_saves", &iconSaves}, {"icon_info", &iconInfo},
        {"icon_help", &iconHelp}, {"icon_picture", &iconPicture}, {"icon_controllers", &iconControllers},
        {"icon_online", &iconOnline}, {"icon_home", &iconHome}, {"icon_sound", &iconSound}, {"icon_power", &iconPower},
        {"background_wide", &backgroundWide}, {"bar_wide", &barWide},
        {"background_shelf", &backgroundShelf}, {"background_shelf_wide", &backgroundShelfWide},
        {"background_plain", &backgroundPlain}, {"background_plain_wide", &backgroundPlainWide},
        {"background_channels", &backgroundChannels}, {"background_channels_wide", &backgroundChannelsWide},
    };
    for (const ThemeImage& im : theme_images()) {
        // The walls: only the set the menu shows (EnsureBackgrounds).
        if (std::strncmp(im.name, "background", 10) == 0) continue;
        for (const Slot& s : slots) {
            if (std::strcmp(s.name, im.name) == 0) *s.tex = Pick(im);
        }
    }
    EnsureBackgrounds();
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
        paint_drop_shadow9(MenuTheme(), c);
        dropShadow9 = Upload(c);
        paint_channel_frame9(MenuTheme(), false, c);
        channelFrame9 = Upload(c);
        paint_channel_frame9(MenuTheme(), true, c);
        channelFrameOver9 = Upload(c);
        paint_label9(MenuTheme(), c);
        label9 = Upload(c);
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

// A theme's walls are 1.2 MB (640x480) or 1.6 MB (856x480) each, and the
// menu's MEM2 is never given back: each is read the first time the menu
// can show it. On a widescreen menu a wall's _wide picture when the theme
// has one, else its 640 one; on a 4:3 menu the 640 one. Switching the
// menu's shape (Settings) reads the other set then.
void EnsureBackgrounds() {
    struct Wall {
        const char* name;
        const char* wide_name;
        Tex* tex;
        Tex* wide_tex;
        bool tried, tried_wide;
    };
    static Wall walls[] = {
        {"background", "background_wide", &background, &backgroundWide, false, false},
        {"background_shelf", "background_shelf_wide", &backgroundShelf, &backgroundShelfWide, false, false},
        {"background_plain", "background_plain_wide", &backgroundPlain, &backgroundPlainWide, false, false},
        {"background_channels", "background_channels_wide", &backgroundChannels, &backgroundChannelsWide, false, false},
    };
    const bool wide = WideMenu();
    const auto load = [](const char* name, Tex* tex) {
        for (const ThemeImage& im : theme_images())
            if (std::strcmp(im.name, name) == 0) *tex = Pick(im);
    };
    for (Wall& w : walls) {
        const bool paintsWide = MenuTheme().clock_dials && std::strcmp(w.wide_name, "background_wide") == 0;
        if (wide && (ThemeImageExists(w.wide_name) || paintsWide)) {
            if (!w.tried_wide) load(w.wide_name, w.wide_tex);
            w.tried_wide = true;
        } else {
            if (!w.tried) load(w.name, w.tex);
            w.tried = true;
        }
    }
}

bool BarBump() { return MenuTheme().bar_bump; }

bool HomeIos6() { return MenuTheme().home_ios6; }

void SetOnHome(bool home) {
    const std::string& tiles = riftwii::wii::Settings().home_tiles;
    // Names stand in the channels' three rows; the carousel and the 3D
    // covers' ring stand free of any row (a shelf would cut through them).
    g_homeWall = tiles == "shelf"                          ? HomeWall::Shelf
                 : tiles == "channels" || tiles == "names" ? HomeWall::Channels
                 : tiles == "carousel" || tiles == "covers3d" ? HomeWall::Bare
                                                            : HomeWall::Plain;
    g_onHome = home;
}

bool Ready() { return g_ready; }

GXColor BannerTint() { return g_banner_tint; }

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

void DrawRgb5a3Part(const u8* data, int w, int h, float u0, float u1, float x, float y, float dw, float dh,
                    int alpha) {
    const u8 a = static_cast<u8>(alpha < 0 ? 0 : alpha > 255 ? 255 : alpha);
    DrawRgb5a3Region(data, w, h, u0, 0.0f, u1, 1.0f, x, y, dw, dh, (GXColor){0xFF, 0xFF, 0xFF, a},
                     (GXColor){0xFF, 0xFF, 0xFF, a});
}

void DrawRgb5a3Region(const u8* data, int w, int h, float u0, float v0, float u1, float v1, float x, float y, float dw,
                      float dh, GXColor top, GXColor bottom) {
    if (!data || (top.a == 0 && bottom.a == 0)) return;
    GXTexObj tex;
    GX_InitTexObj(&tex, const_cast<u8*>(data), static_cast<u16>(w), static_cast<u16>(h), GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP,
                  GX_FALSE);
    GX_InitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&tex, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    // libgui's 2D view: the identity moved to z = -50 (video.cpp).
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x, y, 0);
    GX_Color4u8(top.r, top.g, top.b, top.a);
    GX_TexCoord2f32(u0, v0);
    GX_Position3f32(x + dw, y, 0);
    GX_Color4u8(top.r, top.g, top.b, top.a);
    GX_TexCoord2f32(u1, v0);
    GX_Position3f32(x + dw, y + dh, 0);
    GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
    GX_TexCoord2f32(u1, v1);
    GX_Position3f32(x, y + dh, 0);
    GX_Color4u8(bottom.r, bottom.g, bottom.b, bottom.a);
    GX_TexCoord2f32(u0, v1);
    GX_End();
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
    if (!g_onHome || g_homeWall == HomeWall::Bare) {
        // Every other screen: the theme's plain wall when it has one
        // (Bookshelf: no shelves behind Settings' panels).
        if (wide && backgroundPlainWide.data) pick = &backgroundPlainWide;
        else if (backgroundPlain.data && !(wide && backgroundWide.data)) pick = &backgroundPlain;
    } else if (g_homeWall == HomeWall::Shelf) {
        if (wide && backgroundShelfWide.data) pick = &backgroundShelfWide;
        else if (backgroundShelf.data && !(wide && backgroundWide.data)) pick = &backgroundShelf;
    } else if (g_homeWall == HomeWall::Channels) {
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
