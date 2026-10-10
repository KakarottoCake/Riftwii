// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bannerplay.hpp"

#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "banners.hpp"
#include "log.hpp"
#include "riftwii/tpl.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

constexpr float kIconW = 128.0f, kIconH = 96.0f;
constexpr float kBannerW = 608.0f, kBannerH = 456.0f;  // a banner's view, in layout units

bool EndsWith(const std::string& s, const char* tail) {
    const std::size_t n = std::strlen(tail);
    if (s.size() < n) return false;
    for (std::size_t i = 0; i < n; ++i) {
        char a = s[s.size() - n + i], b = tail[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

u8 Wrap(std::uint8_t w) { return w == 1 ? GX_REPEAT : w == 2 ? GX_MIRROR : GX_CLAMP; }

GXColorS10 S10(const std::int16_t c[4]) {
    return GXColorS10{c[0], c[1], c[2], c[3]};
}

// GX has no stencil: a rounded clip is drawn into the depth buffer
// instead. The shape is written nearer than libgui's 2D (which all lies at
// one depth), the banner drawn where the depth equals it, and the shape
// written back at libgui's depth so nothing drawn later is hidden.
constexpr float kClipZ = 20.0f;

// A corner of a quad on its way to the GPU: where, its colour, and each
// texture coordinate it carries.
struct ClipVert {
    float x, y;
    float c[4];
    float uv[8][2];
};

ClipVert Lerp(const ClipVert& a, const ClipVert& b, float t, int ngen) {
    ClipVert o;
    o.x = a.x + (b.x - a.x) * t;
    o.y = a.y + (b.y - a.y) * t;
    for (int i = 0; i < 4; ++i) o.c[i] = a.c[i] + (b.c[i] - a.c[i]) * t;
    for (int g = 0; g < ngen; ++g) {
        o.uv[g][0] = a.uv[g][0] + (b.uv[g][0] - a.uv[g][0]) * t;
        o.uv[g][1] = a.uv[g][1] + (b.uv[g][1] - a.uv[g][1]) * t;
    }
    return o;
}

// A quad (TL, TR, BR, BL) cut to the box before it is drawn, its colours
// and texture coordinates cut with it, then drawn as a fan. A console
// draws a polygon that runs far past the screen wrongly at times (Dolphin
// draws it right; the menu's bands had it, 7f1ad1d): banner panes run past
// the screen (a background wider than it, a pane scaled up), and their
// textures flashed on testers' consoles only.
void DrawClipped(const ClipVert (&quad)[4], int ngen, float z, float x0, float y0, float x1, float y1) {
    ClipVert a[12], b[12];
    int n = 4;
    for (int i = 0; i < 4; ++i) a[i] = quad[i];
    // Each edge of the box in turn (Sutherland-Hodgman): what is inside
    // stays, and where an edge of the polygon crosses, a corner is added.
    for (int edge = 0; edge < 4 && n > 0; ++edge) {
        const auto inside = [&](const ClipVert& v) {
            return edge == 0 ? v.x >= x0 : edge == 1 ? v.x <= x1 : edge == 2 ? v.y >= y0 : v.y <= y1;
        };
        const auto cross = [&](const ClipVert& p, const ClipVert& q) {
            const float t = edge == 0 ? (x0 - p.x) / (q.x - p.x)
                            : edge == 1 ? (x1 - p.x) / (q.x - p.x)
                            : edge == 2 ? (y0 - p.y) / (q.y - p.y)
                                        : (y1 - p.y) / (q.y - p.y);
            return Lerp(p, q, t, ngen);
        };
        int m = 0;
        for (int i = 0; i < n && m < 11; ++i) {
            const ClipVert& p = a[i];
            const ClipVert& q = a[(i + 1) % n];
            const bool pin = inside(p), qin = inside(q);
            if (pin) b[m++] = p;
            if (pin != qin && m < 12) b[m++] = cross(p, q);
        }
        n = m;
        for (int i = 0; i < n; ++i) a[i] = b[i];
    }
    if (n < 3) return;
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, static_cast<u16>(n));
    for (int i = 0; i < n; ++i) {
        const ClipVert& v = a[i];
        GX_Position3f32(v.x, v.y, z);
        const auto u8c = [](float f) { return static_cast<u8>(f < 0 ? 0 : f > 255 ? 255 : f + 0.5f); };
        GX_Color4u8(u8c(v.c[0]), u8c(v.c[1]), u8c(v.c[2]), u8c(v.c[3]));
        for (int g = 0; g < ngen; ++g) GX_TexCoord2f32(v.uv[g][0], v.uv[g][1]);
    }
    GX_End();
}

void RoundShape(const RoundClip& c, float z) {
    constexpr int kSteps = 8;  // per corner
    const float r = std::min(c.radius, std::min(c.w, c.h) / 2);
    const float cx[4] = {c.x + c.w - r, c.x + r, c.x + r, c.x + c.w - r};
    const float cy[4] = {c.y + r, c.y + r, c.y + c.h - r, c.y + c.h - r};
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetNumTevStages(1);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 2 + 4 * (kSteps + 1));
    GX_Position3f32(c.x + c.w / 2, c.y + c.h / 2, z);
    GX_Color4u8(0, 0, 0, 0);
    // Corners from the top right, anticlockwise on screen.
    for (int k = 0; k < 4; ++k) {
        for (int s = 0; s <= kSteps; ++s) {
            const float a = (k * 90.0f + s * 90.0f / kSteps) * 3.14159265f / 180.0f;
            GX_Position3f32(cx[k] + r * std::cos(a), cy[k] - r * std::sin(a), z);
            GX_Color4u8(0, 0, 0, 0);
        }
    }
    GX_Position3f32(cx[0] + r, cy[0], z);
    GX_Color4u8(0, 0, 0, 0);
    GX_End();
}

void DepthOnly(bool on) {
    GX_SetColorUpdate(on ? GX_FALSE : GX_TRUE);
    GX_SetAlphaUpdate(on ? GX_FALSE : GX_TRUE);
}

// A material's own TEV stages, as its 16-byte records give them: the
// texture coordinate, colour channel and texture map (0xFF none), the
// swap selections, then the colour and the alpha combiners, each as
// inputs b|a, d|c, then scale (2 bits), bias (2) and operation (4), then
// the constant selection (5), output register (2) and clamp (1).
// (Field order as USB Loader GX's banner code reads them, for insight.)
void MaterialTev(const std::vector<std::array<std::uint8_t, 16>>& stages) {
    const std::size_t count = std::min<std::size_t>(stages.size(), 16);
    GX_SetNumTevStages(static_cast<u8>(count));
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* t = stages[i].data();
        const u8 stage = static_cast<u8>(GX_TEVSTAGE0 + i);
        // libogc looks the colour channel up in a 9-entry table
        // (_gxtevcolid): a byte past GX_ALPHA_BUMPN would read beyond it.
        GX_SetTevOrder(stage, t[0] == 0xFF ? GX_TEXCOORDNULL : t[0], t[2] == 0xFF ? GX_TEXMAP_NULL : t[2],
                       t[1] <= GX_ALPHA_BUMPN ? t[1] : GX_COLORNULL);
        GX_SetTevSwapMode(stage, (t[3] >> 1) & 3, (t[3] >> 3) & 3);
        GX_SetTevColorIn(stage, t[4] & 15, t[4] >> 4, t[5] & 15, t[5] >> 4);
        GX_SetTevColorOp(stage, t[6] & 15, (t[6] >> 4) & 3, t[6] >> 6, t[7] & 1, (t[7] >> 1) & 3);
        GX_SetTevKColorSel(stage, t[7] >> 3);
        GX_SetTevAlphaIn(stage, t[8] & 15, t[8] >> 4, t[9] & 15, t[9] >> 4);
        GX_SetTevAlphaOp(stage, t[10] & 15, (t[10] >> 4) & 3, t[10] >> 6, t[11] & 1, (t[11] >> 1) & 3);
        GX_SetTevKAlphaSel(stage, t[11] >> 3);
        GX_SetTevDirect(stage);
    }
}

// GX's own swap tables: as is, then red, green and blue for every channel.
void DefaultSwapTables() {
    GX_SetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
    GX_SetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
    GX_SetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
    GX_SetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
}

// The TEV back to libgui's single pass-colour stage.
void PlainTev() {
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    for (int i = 1; i < 8; ++i) GX_SetVtxDesc(static_cast<u8>(GX_VA_TEX0 + i), GX_NONE);
    for (int i = 0; i < 16; ++i) GX_SetTevSwapMode(static_cast<u8>(GX_TEVSTAGE0 + i), GX_TEV_SWAP0, GX_TEV_SWAP0);
    GX_SetNumTexGens(1);
    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
}

}  // namespace

BannerPlayer::~BannerPlayer() { Free(); }

void BannerPlayer::Free() {
    for (auto& t : textures_) free(t.second.own);
    textures_.clear();
    by_index_.clear();
    looked_up_.clear();
    free(arc_);
    arc_ = nullptr;
    arc_size_ = 0;
    loaded_ = false;
}

bool BannerPlayer::Load(const std::vector<std::uint8_t>& opening_bnr, bool icon, std::string& error) {
    // A banner that asks for more memory than is free shows as a name tile
    // instead of ending the menu.
    try {
        return LoadParts(opening_bnr, icon, error);
    } catch (const std::bad_alloc&) {
        Free();
        error = "out of memory for the banner";
        return false;
    }
}

bool BannerPlayer::LoadParts(const std::vector<std::uint8_t>& opening_bnr, bool icon, std::string& error) {
    Free();
    icon_ = icon;
    OpeningBanner b;
    if (!parse_opening_bnr(opening_bnr.data(), opening_bnr.size(), b, error, !icon)) return false;
    name_ = b.names[BannerLanguageIndex()];
    if (name_.empty()) name_ = b.names[kBnrEnglish];
    const std::vector<std::uint8_t>& arc = icon ? b.icon : b.banner;
    if (arc.empty()) {
        error = icon ? "no icon" : "no banner";
        return false;
    }
    // GX reads textures where they lie: the archive in 32-byte aligned memory.
    arc_ = static_cast<std::uint8_t*>(memalign(32, (arc.size() + 31) & ~std::size_t(31)));
    if (!arc_) {
        error = "out of memory for the banner";
        return false;
    }
    std::memcpy(arc_, arc.data(), arc.size());
    arc_size_ = arc.size();
    if (!U8Archive::parse(arc_, arc_size_, u8_, error)) {
        Free();
        return false;
    }
    const std::vector<std::string> layouts = u8_.files_in("/arc/blyt", ".brlyt");
    const std::uint8_t* d = nullptr;
    std::size_t n = 0;
    if (layouts.empty() || !u8_.find(layouts[0], d, n) || !parse_brlyt(d, n, base_, error)) {
        if (error.empty()) error = "no layout";
        Free();
        return false;
    }
    language_ = language_panes(base_, BannerLanguageCode());
    apply_language(base_, language_);
    has_start_ = has_loop_ = false;
    for (const std::string& path : u8_.files_in("/arc/anim", ".brlan")) {
        if (!u8_.find(path, d, n)) continue;
        Animation a;
        std::string why;
        if (!parse_brlan(d, n, a, why)) {
            if (!icon) logf("Banner: %s not played: %s\n", path.c_str(), why.c_str());
            continue;
        }
        if (EndsWith(path, "_start.brlan")) {
            start_ = std::move(a);
            has_start_ = true;
        } else if (!has_loop_) {
            loop_ = std::move(a);
            has_loop_ = true;
        } else if (!icon) {
            logf("Banner: %s not played (one loop is)\n", path.c_str());
        }
    }
    // What it uses, for a problem report (banners that look wrong on a
    // console look right in Dolphin).
    if (!icon) {
        logf("Banner: %s\n", describe_banner(base_, has_start_ ? &start_ : nullptr, has_loop_ ? &loop_ : nullptr).c_str());
    }
    DCFlushRange(arc_, (arc_size_ + 31) & ~std::size_t(31));
    GX_InvalidateTexAll();
    work_ = base_;
    if (has_start_) start_binding_ = bind_animation(start_, base_);
    if (has_loop_) loop_binding_ = bind_animation(loop_, base_);
    by_index_.clear();
    looked_up_.clear();
    loaded_ = true;
    Restart();
    return true;
}

void BannerPlayer::Restart() {
    frame_ = -1;
    Step();
}

void BannerPlayer::Step() {
    if (!loaded_) return;
    ++frame_;
    reset_animated(base_, work_);
    const int start_frames = has_start_ ? start_.frames : 0;
    if (has_start_)
        apply_animation(start_, static_cast<float>(frame_ < start_frames ? frame_ : start_frames), work_, start_binding_);
    if (has_loop_ && frame_ >= start_frames) {
        const int length = loop_.frames > 0 ? loop_.frames : 1;
        apply_animation(loop_, static_cast<float>((frame_ - start_frames) % length), work_, loop_binding_);
    }
    // Language groups again: an animation may have shown a hidden one.
    apply_language(work_, language_);
}

BannerPlayer::Texture* BannerPlayer::TextureOf(std::size_t index) {
    if (index >= work_.textures.size()) return nullptr;
    // By index after the first time (a texture-pattern track may add names).
    if (index < looked_up_.size() && looked_up_[index]) return by_index_[index];
    if (looked_up_.size() < work_.textures.size()) {
        looked_up_.resize(work_.textures.size(), false);
        by_index_.resize(work_.textures.size(), nullptr);
    }
    looked_up_[index] = true;
    by_index_[index] = TextureByName(work_.textures[index]);
    return by_index_[index];
}

BannerPlayer::Texture* BannerPlayer::TextureByName(const std::string& name) {
    auto it = textures_.find(name);
    if (it != textures_.end()) return it->second.ok ? &it->second : nullptr;
    Texture& t = textures_[name];
    const std::uint8_t* d = nullptr;
    std::size_t n = 0;
    std::vector<TplImage> images;
    std::string error;
    if (!u8_.find("/arc/timg/" + name, d, n) || !parse_tpl(d, n, images, error)) {
        logf("Banner: texture %s: %s; what uses it is left out\n", name.c_str(), error.empty() ? "not in it" : error.c_str());
        return nullptr;
    }
    const TplImage& im = images[0];
    const std::uint8_t* data = d + im.data_offset;
    if (reinterpret_cast<std::uintptr_t>(data) & 31) {
        t.own = static_cast<std::uint8_t*>(memalign(32, (im.data_size + 31) & ~std::size_t(31)));
        if (!t.own) {
            logf("Banner: texture %s: out of memory; what uses it is left out\n", name.c_str());
            return nullptr;
        }
        std::memcpy(t.own, data, im.data_size);
        DCFlushRange(t.own, (im.data_size + 31) & ~std::size_t(31));
        data = t.own;
    }
    t.ci = im.format == kTplCI4 || im.format == kTplCI8 || im.format == kTplCI14x2;
    if (t.ci) {
        // Palettes must be 32-byte aligned as well; the archive's are in practice.
        const std::uint8_t* pal = d + im.palette_offset;
        if (reinterpret_cast<std::uintptr_t>(pal) & 31) {
            logf("Banner: texture %s: its palette is not 32-byte aligned; what uses it is left out\n", name.c_str());
            return nullptr;
        }
        GX_InitTlutObj(&t.tlut, const_cast<std::uint8_t*>(pal), static_cast<u8>(im.palette_format), im.palette_count);
        GX_InitTexObjCI(&t.obj, const_cast<std::uint8_t*>(data), im.width, im.height, static_cast<u8>(im.format), GX_CLAMP,
                        GX_CLAMP, GX_FALSE, GX_TLUT0);
    } else {
        GX_InitTexObj(&t.obj, const_cast<std::uint8_t*>(data), im.width, im.height, static_cast<u8>(im.format), GX_CLAMP,
                      GX_CLAMP, GX_FALSE);
    }
    t.ok = true;
    return &t;
}

void BannerPlayer::Draw(float x, float y, float w, float h, int alpha, const RoundClip* clip) {
    if (!loaded_ || alpha <= 0) return;
    float z = 0;
    if (clip) {
        Mtx flat;
        guMtxIdentity(flat);
        guMtxTransApply(flat, flat, 0.0f, 0.0f, -50.0f);
        GX_LoadPosMtxImm(flat, GX_PNMTX0);
        DepthOnly(true);
        GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        RoundShape(*clip, kClipZ);
        DepthOnly(false);
        GX_SetZMode(GX_TRUE, GX_EQUAL, GX_FALSE);
        z = kClipZ;
    }
    // The shown area in layout units (y up, the layout's centre at 0, 0).
    // A banner shows 608 x 456 of its layout, whatever its own size or the
    // box's shape: on a 16:9 menu the Wii Menu stretches those 608 over the
    // whole screen and narrows back the panes flagged to keep their shape
    // (measured against its Disc Channel in Dolphin, Wii Party and Mario
    // Party 8).
    const float rh = icon_ ? kIconH : kBannerH;
    const float rw = icon_ ? kIconW : kBannerW;
    const float sx = w / rw, sy = h / rh;
    const float left = -rw / 2, top = rh / 2;
    const float bx = clip ? clip->x : x, by = clip ? clip->y : y, bw = clip ? clip->w : w, bh = clip ? clip->h : h;
    Menu_Scissor(bx, by, bw, bh);
    const float boxX0 = bx - 1, boxY0 = by - 1, boxX1 = bx + bw + 1, boxY1 = by + bh + 1;
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    for (int i = 1; i < 8; ++i) GX_SetVtxAttrFmt(GX_VTXFMT0, static_cast<u8>(GX_VA_TEX0 + i), GX_TEX_ST, GX_F32, 0);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    // The narrowing that keeps a flagged pane's shape in that stretch; an
    // icon's in a box wider than 4:3 (a widescreen menu's channel tile).
    layout_quads(work_, quads_, scratch_, (wide_ || icon_) && sx > 0 ? sy / sx : 1.0f);
    // A vertex's colour: the quad's, or the material's by its channel
    // control, times the banner's own fade.
    const auto vertex_color = [alpha](const LytQuad& q, const LytMaterial* m, int k) {
        LytColor c = q.color[k];
        if (m && m->has_channel) {
            const LytColor mc = m->has_material_color ? m->material_color : LytColor{};
            if (m->color_source == 0) {
                c.r = mc.r;
                c.g = mc.g;
                c.b = mc.b;
            }
            if (m->alpha_source == 0) c.a = static_cast<std::uint8_t>(mc.a * c.a / 255);
        }
        c.a = static_cast<u8>(c.a * alpha / 255);
        return c;
    };
    // TL, TR, BR, BL around the quad.
    static const int kOrder[4] = {0, 1, 3, 2};
    for (const LytQuad& q : quads_) {
        const LytMaterial* m = q.material >= 0 ? &work_.materials[q.material] : nullptr;
        // A material with its own TEV stages: its textures in their maps,
        // a coordinate for each of its texture coordinate generators (its
        // UV set through its SRT), its colours in the registers, its
        // stages as given.
        if (m && !m->tev.empty() && m->tev.size() <= 16 && m->maps.size() <= 8) {
            bool ok = true;
            for (std::size_t i = 0; i < m->maps.size() && ok; ++i) {
                Texture* t = TextureOf(m->maps[i].texture);
                if (!t) {
                    ok = false;
                    break;
                }
                GX_InitTexObjWrapMode(&t->obj, Wrap(m->maps[i].wrap_s), Wrap(m->maps[i].wrap_t));
                if (t->ci) {
                    GX_InitTexObjTlut(&t->obj, static_cast<u32>(GX_TLUT0 + i));
                    GX_LoadTlut(&t->tlut, static_cast<u32>(GX_TLUT0 + i));
                }
                GX_LoadTexObj(&t->obj, static_cast<u8>(GX_TEXMAP0 + i));
            }
            if (!ok) continue;
            // Each generator: its UV set (source GX_TG_TEX0 on) and its SRT
            // (matrix GX_TEXMTX0 on, three apart); none listed, one per map.
            struct Gen {
                int uv = 0;
                int srt = -1;
            } gens[8];
            std::size_t ngen = m->texgens.empty() ? m->maps.size() : std::min<std::size_t>(m->texgens.size(), 8);
            for (std::size_t i = 0; i < ngen; ++i) {
                if (m->texgens.empty()) {
                    gens[i].uv = 0;
                    gens[i].srt = i < m->srts.size() ? static_cast<int>(i) : -1;
                } else {
                    const std::array<std::uint8_t, 4>& g = m->texgens[i];
                    gens[i].uv = g[1] >= GX_TG_TEX0 && g[1] <= GX_TG_TEX7 ? g[1] - GX_TG_TEX0 : 0;
                    gens[i].srt = g[2] >= GX_TEXMTX0 && g[2] < GX_IDENTITY ? (g[2] - GX_TEXMTX0) / 3 : -1;
                    if (gens[i].srt >= static_cast<int>(m->srts.size())) gens[i].srt = -1;
                }
            }
            GX_SetNumTexGens(static_cast<u32>(ngen));
            for (std::size_t i = 0; i < 8; ++i) {
                if (i < ngen)
                    GX_SetTexCoordGen(static_cast<u16>(GX_TEXCOORD0 + i), GX_TG_MTX2x4, static_cast<u32>(GX_TG_TEX0 + i), GX_IDENTITY);
                GX_SetVtxDesc(static_cast<u8>(GX_VA_TEX0 + i), i < ngen ? GX_DIRECT : GX_NONE);
            }
            GX_SetTevColorS10(GX_TEVREG0, S10(m->black));
            GX_SetTevColorS10(GX_TEVREG1, S10(m->white));
            GX_SetTevColorS10(GX_TEVREG2, S10(m->color3));
            for (int i = 0; i < 4; ++i) {
                const LytColor& k = m->tev_k[i];
                GX_SetTevKColor(static_cast<u8>(GX_KCOLOR0 + i), (GXColor){k.r, k.g, k.b, k.a});
            }
            // Its swap tables: which channel each of a texel's or the
            // raster colour's channels is taken from (a stage picks one).
            if (m->has_swap) {
                for (int i = 0; i < 4; ++i) {
                    const std::uint8_t t = m->swap[static_cast<std::size_t>(i)];
                    GX_SetTevSwapModeTable(static_cast<u8>(GX_TEV_SWAP0 + i), t & 3, (t >> 2) & 3, (t >> 4) & 3, t >> 6);
                }
            }
            MaterialTev(m->tev);
            if (m->has_blend) GX_SetBlendMode(m->blend[0], m->blend[1], m->blend[2], m->blend[3]);
            else GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
            if (m->has_alpha_compare) {
                const std::array<std::uint8_t, 4>& a = m->alpha_compare;
                GX_SetAlphaCompare(a[0] & 7, a[2], a[1], (a[0] >> 4) & 7, a[3]);
            } else {
                GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
            }
            ClipVert corners[4];
            for (int n = 0; n < 4; ++n) {
                const int k = kOrder[n];
                ClipVert& cv = corners[n];
                const LytColor c = vertex_color(q, m, k);
                cv.x = x + (q.x[k] - left) * sx;
                cv.y = y + (top - q.y[k]) * sy;
                cv.c[0] = c.r;
                cv.c[1] = c.g;
                cv.c[2] = c.b;
                cv.c[3] = c.a;
                for (std::size_t i = 0; i < ngen; ++i) {
                    const std::array<float, 8>* uv =
                        (q.uvs && static_cast<std::size_t>(gens[i].uv) < q.uvs->size()) ? &(*q.uvs)[gens[i].uv] : nullptr;
                    const float u = uv ? (*uv)[2 * k] : (k & 1 ? 1.0f : 0.0f);
                    const float v = uv ? (*uv)[2 * k + 1] : (k & 2 ? 1.0f : 0.0f);
                    if (gens[i].srt < 0) {
                        cv.uv[i][0] = u;
                        cv.uv[i][1] = v;
                        continue;
                    }
                    const LytTexSrt& t = m->srts[static_cast<std::size_t>(gens[i].srt)];
                    const float rr = t.rotate * 3.14159265f / 180.0f, c2 = std::cos(rr), s2 = std::sin(rr);
                    const float du = (u - 0.5f) * t.sx, dv = (v - 0.5f) * t.sy;
                    cv.uv[i][0] = du * c2 - dv * s2 + 0.5f + t.tx;
                    cv.uv[i][1] = du * s2 + dv * c2 + 0.5f + t.ty;
                }
            }
            DrawClipped(corners, static_cast<int>(ngen), z, boxX0, boxY0, boxX1, boxY1);
            if (m->has_swap) DefaultSwapTables();
            for (std::size_t i = 1; i < 8; ++i) GX_SetVtxDesc(static_cast<u8>(GX_VA_TEX0 + i), GX_NONE);
            GX_SetNumTexGens(1);
            GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
            continue;
        }
        Texture* tex = nullptr;
        LytTexSrt srt;
        if (m && !m->maps.empty()) {
            tex = TextureOf(m->maps[0].texture);
            if (tex) {
                GX_InitTexObjWrapMode(&tex->obj, Wrap(m->maps[0].wrap_s), Wrap(m->maps[0].wrap_t));
                if (tex->ci) GX_LoadTlut(&tex->tlut, GX_TLUT0);
                GX_LoadTexObj(&tex->obj, GX_TEXMAP0);
            }
            if (!m->srts.empty()) srt = m->srts[0];
        }
        const std::int16_t kBlack[4] = {0, 0, 0, 0}, kWhite[4] = {255, 255, 255, 255};
        GX_SetTevColorS10(GX_TEVREG0, S10(m ? m->black : kBlack));
        GX_SetTevColorS10(GX_TEVREG1, S10(m ? m->white : kWhite));
        // Stage 0: the texture's black to white mapped onto C0 to C1 (no
        // texture: C1). Stage 1: times the rasterised colour.
        GX_SetNumTevStages(2);
        GX_SetTevOrder(GX_TEVSTAGE0, tex ? GX_TEXCOORD0 : GX_TEXCOORDNULL, tex ? GX_TEXMAP0 : GX_TEXMAP_NULL, GX_COLOR0A0);
        if (tex) {
            GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_C0, GX_CC_C1, GX_CC_TEXC, GX_CC_ZERO);
            GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_A0, GX_CA_A1, GX_CA_TEXA, GX_CA_ZERO);
        } else {
            GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C1);
            GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A1);
        }
        GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_CPREV, GX_CC_RASC, GX_CC_ZERO);
        GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_RASA, GX_CA_ZERO);
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        // A second texture is the alpha mask (riftwii/brlyt.hpp): stage 2
        // keeps the colour and scales the alpha by that texture's.
        // (A palette mask would need the colour texture's TLUT slot: left out.)
        Texture* mask = (tex && m->maps.size() >= 2) ? TextureOf(m->maps[1].texture) : nullptr;
        if (mask && mask->ci) mask = nullptr;
        if (mask) {
            GX_InitTexObjWrapMode(&mask->obj, Wrap(m->maps[1].wrap_s), Wrap(m->maps[1].wrap_t));
            GX_LoadTexObj(&mask->obj, GX_TEXMAP1);
            GX_SetNumTevStages(3);
            GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
            GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
            GX_SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_APREV, GX_CA_TEXA, GX_CA_ZERO);
            GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
            GX_SetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        }
        if (m && m->has_blend) {
            GX_SetBlendMode(m->blend[0], m->blend[1], m->blend[2], m->blend[3]);
        } else {
            GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
        }
        if (m && m->has_alpha_compare) {
            const std::array<std::uint8_t, 4>& a = m->alpha_compare;
            GX_SetAlphaCompare(a[0] & 7, a[2], a[1], (a[0] >> 4) & 7, a[3]);
        } else {
            GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
        }
        GX_SetVtxDesc(GX_VA_TEX0, tex ? GX_DIRECT : GX_NONE);
        const std::array<float, 8>* uv = (q.uvs && !q.uvs->empty()) ? &(*q.uvs)[0] : nullptr;
        const float r = srt.rotate * 3.14159265f / 180.0f, cr = std::cos(r), sr = std::sin(r);
        ClipVert corners[4];
        for (int n = 0; n < 4; ++n) {
            const int k = kOrder[n];
            ClipVert& cv = corners[n];
            const LytColor c = vertex_color(q, m, k);
            cv.x = x + (q.x[k] - left) * sx;
            cv.y = y + (top - q.y[k]) * sy;
            cv.c[0] = c.r;
            cv.c[1] = c.g;
            cv.c[2] = c.b;
            cv.c[3] = c.a;
            if (tex) {
                const float u = uv ? (*uv)[2 * k] : (k & 1 ? 1.0f : 0.0f);
                const float v = uv ? (*uv)[2 * k + 1] : (k & 2 ? 1.0f : 0.0f);
                const float du = (u - 0.5f) * srt.sx, dv = (v - 0.5f) * srt.sy;
                cv.uv[0][0] = du * cr - dv * sr + 0.5f + srt.tx;
                cv.uv[0][1] = du * sr + dv * cr + 0.5f + srt.ty;
            }
        }
        DrawClipped(corners, tex ? 1 : 0, z, boxX0, boxY0, boxX1, boxY1);
    }
    PlainTev();
    // The whole screen again before the clip's depth is put back: under
    // the box set above, the edges of the shape the clip wrote (drawn
    // before it) stayed at its depth, and later drawing at libgui's depth
    // failed there: a pixel-wide line along each tile's right and bottom
    // edges, seen through the next screen (Channels' banner).
    GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());
    if (clip) {
        // The clip's depth back to libgui's, so later drawing is not hidden.
        DepthOnly(true);
        GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        RoundShape(*clip, 0);
        DepthOnly(false);
        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    }
}

}  // namespace riftwii::wii
