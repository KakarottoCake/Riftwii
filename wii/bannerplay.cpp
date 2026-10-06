// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "bannerplay.hpp"

#include <malloc.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "banners.hpp"
#include "riftwii/tpl.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

constexpr float kIconW = 128.0f, kIconH = 96.0f;

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

// The TEV back to libgui's single pass-colour stage.
void PlainTev() {
    GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
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
        if (!parse_brlan(d, n, a, why)) continue;
        if (EndsWith(path, "_start.brlan")) {
            start_ = std::move(a);
            has_start_ = true;
        } else if (!has_loop_) {
            loop_ = std::move(a);
            has_loop_ = true;
        }
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
    if (!u8_.find("/arc/timg/" + name, d, n) || !parse_tpl(d, n, images, error)) return nullptr;
    const TplImage& im = images[0];
    const std::uint8_t* data = d + im.data_offset;
    if (reinterpret_cast<std::uintptr_t>(data) & 31) {
        t.own = static_cast<std::uint8_t*>(memalign(32, (im.data_size + 31) & ~std::size_t(31)));
        if (!t.own) return nullptr;
        std::memcpy(t.own, data, im.data_size);
        DCFlushRange(t.own, (im.data_size + 31) & ~std::size_t(31));
        data = t.own;
    }
    t.ci = im.format == kTplCI4 || im.format == kTplCI8 || im.format == kTplCI14x2;
    if (t.ci) {
        // Palettes must be 32-byte aligned as well; the archive's are in practice.
        const std::uint8_t* pal = d + im.palette_offset;
        if (reinterpret_cast<std::uintptr_t>(pal) & 31) return nullptr;
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
    const float rw = icon_ ? kIconW : work_.width, rh = icon_ ? kIconH : work_.height;
    const float sx = w / rw, sy = h / rh;
    const float left = -rw / 2, top = rh / 2;
    const float bx = clip ? clip->x : x, by = clip ? clip->y : y, bw = clip ? clip->w : w, bh = clip ? clip->h : h;
    Menu_Scissor(bx, by, bw, bh);
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetNumChans(1);
    GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
    layout_quads(work_, quads_, scratch_);
    for (const LytQuad& q : quads_) {
        const LytMaterial* m = q.material >= 0 ? &work_.materials[q.material] : nullptr;
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
        // TL, TR, BR, BL around the quad.
        static const int kOrder[4] = {0, 1, 3, 2};
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        for (const int k : kOrder) {
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
            GX_Position3f32(x + (q.x[k] - left) * sx, y + (top - q.y[k]) * sy, z);
            GX_Color4u8(c.r, c.g, c.b, static_cast<u8>(c.a * alpha / 255));
            if (tex) {
                const float u = uv ? (*uv)[2 * k] : (k & 1 ? 1.0f : 0.0f);
                const float v = uv ? (*uv)[2 * k + 1] : (k & 2 ? 1.0f : 0.0f);
                const float du = (u - 0.5f) * srt.sx, dv = (v - 0.5f) * srt.sy;
                GX_TexCoord2f32(du * cr - dv * sr + 0.5f + srt.tx, du * sr + dv * cr + 0.5f + srt.ty);
            }
        }
        GX_End();
    }
    PlainTev();
    if (clip) {
        // The clip's depth back to libgui's, so later drawing is not hidden.
        DepthOnly(true);
        GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
        RoundShape(*clip, 0);
        DepthOnly(false);
        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    }
    GX_SetScissor(0, 0, Menu_XfbWidth(), Menu_EfbHeight());
}

}  // namespace riftwii::wii
