// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "transition.hpp"

#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <unistd.h>

#include <algorithm>
#include <cmath>

#include "log.hpp"
#include "skin.hpp"
#include "video.h"

namespace riftwii::wii::transition {
namespace {

// The pictures of the last frames: one is the old screen of the running
// transition, the other takes each new frame. A transition starting in the
// middle of another takes the frame as it was on screen, both mixed.
u8* g_pic[2] = {nullptr, nullptr};
int g_w = 0, g_h = 0;
int g_snap = 1;          // the running transition's picture; frames go to the other
bool g_have = false;     // a frame was kept

// Asked for (any thread, the GUI halted), started by the next frame.
volatile bool g_pending = false;
volatile bool g_pendingAsked = false;  // by Begin: BeginAuto leaves it
Kind g_pendingKind = Kind::Fade;
Rect g_pendingFrom;

// Hold: the last frame kept over the screen until the next one comes.
volatile bool g_hold = false;
bool g_holding = false;  // its picture taken (g_snap)
u64 g_holdSince = 0;
Kind g_holdKind = Kind::Fade;
Rect g_holdFrom;

float g_slow = 1;  // the GUI script's slow motion, for frame-by-frame shots

struct Running {
    bool on = false;
    Kind kind = Kind::Fade;
    Rect from;
    u64 start = 0;
    float ms = 1;
    float t = 1;  // 0 to 1
} g_run;

float Duration(Kind k) {
    switch (k) {
    case Kind::ZoomIn: return 430;
    case Kind::ZoomOut: return 400;
    case Kind::SlideForward:
    case Kind::SlideBack: return 320;
    case Kind::PopOpen: return 170;
    case Kind::PopClose: return 150;
    case Kind::PageForward:
    case Kind::PageBack: return 280;
    case Kind::FromBlack: return 600;
    case Kind::Fade:
    default: return 240;
    }
}

// k * p + (tx, ty), in menu units.
struct Affine {
    float k = 1, tx = 0, ty = 0;
};

Affine Then(const Affine& first, const Affine& second) {  // second after first
    return Affine{second.k * first.k, second.k * first.tx + second.tx, second.k * first.ty + second.ty};
}
Affine Inverse(const Affine& a) { return Affine{1 / a.k, -a.tx / a.k, -a.ty / a.k}; }

// The camera that shows the whole screen inside `from` (its middle at the
// rectangle's, as tall or as wide as the rectangle by its shape).
Affine IntoRect(const Rect& r) {
    const float k = std::max(0.05f, std::sqrt((r.w / 640.0f) * (r.h / 480.0f)));
    return Affine{k, r.x + r.w / 2 - k * 320.0f, r.y + r.h / 2 - k * 240.0f};
}
Affine Mix(const Affine& a, const Affine& b, float e) {
    // The middle of the screen moves straight; the scale changes evenly
    // in how big things look (1 / scale), so a zoom looks steady enough
    // without powf (libm's would cost the DOL kilobytes).
    const float k = 1.0f / (1.0f / a.k + (1.0f / b.k - 1.0f / a.k) * e);
    const float ax = a.k * 320 + a.tx, ay = a.k * 240 + a.ty;
    const float bx = b.k * 320 + b.tx, by = b.k * 240 + b.ty;
    const float cx = ax + (bx - ax) * e, cy = ay + (by - ay) * e;
    return Affine{k, cx - k * 320, cy - k * 240};
}

float Clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
float Smooth(float a, float b, float t) {
    const float x = Clamp01((t - a) / (b - a));
    return x * x * (3 - 2 * x);
}

// Where the old screen goes and how much of it shows, and the new
// screen's camera, at t.
void Pose(const Running& r, Affine& live, Affine& old, float& oldAlpha, float& black) {
    live = old = Affine{};
    oldAlpha = 0;
    black = 0;
    const float t = r.t;
    switch (r.kind) {
    case Kind::ZoomIn: {
        // One camera flying into the tile: the new screen grows out of it
        // while the old one, the tile with it, spreads past the screen.
        const float e = Ease(t);
        const Affine start = IntoRect(r.from);
        live = Mix(start, Affine{}, e);
        old = Then(Inverse(start), live);
        // Gone before the tile it flies into fills the screen (blown up,
        // it is only a smear of the tile's colour).
        oldAlpha = 1 - Smooth(0.0f, 0.4f, t);
        break;
    }
    case Kind::ZoomOut: {
        // Backwards: the old screen shrinks into the tile, the new one
        // closing in around it.
        const float e = Ease(t);
        const Affine end = IntoRect(r.from);
        old = Mix(Affine{}, end, e);
        live = Then(Inverse(end), old);
        oldAlpha = 1 - Smooth(0.35f, 0.95f, t);
        break;
    }
    case Kind::SlideForward:
    case Kind::SlideBack: {
        const float e = EaseOut(t);
        const float dir = r.kind == Kind::SlideForward ? 1.0f : -1.0f;
        constexpr float kDistance = 120.0f;
        live.tx = dir * kDistance * (1 - e);
        old.tx = -dir * kDistance * e;
        oldAlpha = 1 - Smooth(0.0f, 0.75f, t);
        break;
    }
    case Kind::PageForward:
    case Kind::PageBack: {
        // Only the old page moves (the grid slides the new one in itself).
        const float e = EaseOut(t);
        old.tx = (r.kind == Kind::PageForward ? -1.0f : 1.0f) * 150.0f * e;
        oldAlpha = 1 - Smooth(0.0f, 0.6f, t);
        break;
    }
    case Kind::FromBlack:
        black = 1 - Ease(t);
        break;
    case Kind::PopOpen:
    case Kind::PopClose:
    case Kind::Fade:
    default:
        oldAlpha = 1 - Ease(t);
        break;
    }
}

// The kept picture, through `a` (menu units) and at `alpha`, over the
// frame: drawn in the EFB's pixels.
void DrawPicture(int index, const Affine& a, float alpha, const Rect* clip = nullptr) {
    const u8 al = static_cast<u8>(std::lround(Clamp01(alpha) * 255));
    if (al == 0 || !g_pic[index]) return;
    // The frame's corners in menu units, then where `a` puts them, as
    // EFB pixels (the display scale from video.cpp's own conversion, so
    // the spot is exact: a slow zoom does not shimmer).
    const float dx = -320.0f / (Menu_ScreenToMenuX(0) - 320.0f);
    const float dy = -240.0f / (Menu_ScreenToMenuY(0) - 240.0f);
    const auto to_efb = [&](float px, float py, float& ox, float& oy) {
        const float mx = 320 + (px * 640.0f / g_w - 320) / dx, my = 240 + (py * 480.0f / g_h - 240) / dy;
        ox = (320 + (a.k * mx + a.tx - 320) * dx) * g_w / 640.0f;
        oy = (240 + (a.k * my + a.ty - 240) * dy) * g_h / 480.0f;
    };
    float x0, y0, x1, y1;
    to_efb(0, 0, x0, y0);
    to_efb(static_cast<float>(g_w), static_cast<float>(g_h), x1, y1);

    Mtx44 p;
    guOrtho(p, 0, static_cast<f32>(g_h), 0, static_cast<f32>(g_w), 0, 300);
    GX_LoadProjectionMtx(p, GX_ORTHOGRAPHIC);
    Mtx mv;
    guMtxIdentity(mv);
    guMtxTransApply(mv, mv, 0.0f, 0.0f, -50.0f);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);
    GX_InvalidateTexAll();
    GXTexObj tex;
    GX_InitTexObj(&tex, g_pic[index], static_cast<u16>(g_w), static_cast<u16>(g_h), GX_TF_RGB565, GX_CLAMP, GX_CLAMP,
                  GX_FALSE);
    GX_InitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&tex, GX_TEXMAP0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    if (clip) Menu_Scissor(clip->x, clip->y, clip->w, clip->h);
    else GX_SetScissor(0, 0, static_cast<u32>(g_w), static_cast<u32>(g_h));
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x0, y0, 0);
    GX_Color4u8(255, 255, 255, al);
    GX_TexCoord2f32(0, 0);
    GX_Position3f32(x1, y0, 0);
    GX_Color4u8(255, 255, 255, al);
    GX_TexCoord2f32(1, 0);
    GX_Position3f32(x1, y1, 0);
    GX_Color4u8(255, 255, 255, al);
    GX_TexCoord2f32(1, 1);
    GX_Position3f32(x0, y1, 0);
    GX_Color4u8(255, 255, 255, al);
    GX_TexCoord2f32(0, 1);
    GX_End();
    // As Menu_DrawImg leaves it: Menu_DrawRectangle draws untextured
    // without setting it (else the next frame's backdrop came out black).
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetScissor(0, 0, static_cast<u32>(g_w), static_cast<u32>(g_h));
    Menu_LoadOrtho();
}

void Keep(int index) {
    if (!g_pic[index]) return;
    GX_SetTexCopySrc(0, 0, static_cast<u16>(g_w), static_cast<u16>(g_h));
    GX_SetTexCopyDst(static_cast<u16>(g_w), static_cast<u16>(g_h), GX_TF_RGB565, GX_FALSE);
    GX_CopyTex(g_pic[index], GX_FALSE);
    GX_PixModeSync();
    g_have = true;
}

}  // namespace

float Ease(float t) {
    t = Clamp01(t);
    if (t < 0.5f) return 4 * t * t * t;
    const float u = -2 * t + 2;
    return 1 - u * u * u / 2;
}
float EaseOut(float t) {
    t = Clamp01(t);
    const float u = 1 - t;
    return 1 - u * u * u;
}
float EaseBack(float t) {
    t = Clamp01(t);
    constexpr float c1 = 1.70158f, c3 = c1 + 1;
    const float u = t - 1;
    return 1 + c3 * u * u * u + c1 * u * u;
}

void Init() {
    g_w = Menu_XfbWidth();
    g_h = Menu_EfbHeight();
    const u32 size = GX_GetTexBufferSize(static_cast<u16>(g_w), static_cast<u16>(g_h), GX_TF_RGB565, GX_FALSE, 0);
    for (u8*& p : g_pic) p = skin::Mem2Alloc(size);
    if (!g_pic[0] || !g_pic[1]) {
        g_pic[0] = g_pic[1] = nullptr;
        logf("Transitions: no memory for the frame pictures; screens cut\n");
        return;
    }
    logf("Transitions: %dx%d, %u KB\n", g_w, g_h, static_cast<unsigned>(2 * size / 1024));
}

void Begin(Kind kind, Rect from) {
    g_pendingKind = kind;
    g_pendingFrom = from;
    g_pendingAsked = true;
    g_pending = true;
}

void BeginAuto() {
    if (g_pending) return;
    g_pendingKind = g_hold ? g_holdKind : Kind::Fade;
    g_pendingFrom = g_hold ? g_holdFrom : Rect{};
    g_pendingAsked = false;
    g_pending = true;
}

void Hold(Kind next, Rect from) {
    g_holdKind = next;
    g_holdFrom = from;
    g_pending = false;  // what was asked comes after the hold
    g_hold = true;
}

void FrameStart() {
    const u64 now = gettime();
    GX_SetScissor(0, 0, static_cast<u32>(g_w), static_cast<u32>(g_h));
    if (g_hold && !g_holding && !g_pending) {
        // The frame last kept stays up.
        if (g_have) {
            g_snap = 1 - g_snap;
            g_holding = true;
            g_holdSince = now;
            g_run.on = false;
        } else {
            g_hold = false;
        }
    }
    // Never held for good: a screen that never came.
    if (g_holding && !g_pending && ticks_to_millisecs(now - g_holdSince) > 4000) BeginAuto();
    if (g_pending) {
        g_pending = false;
        const Kind k = g_pendingKind;
        // A zoom with no tile to zoom from crossfades.
        const bool noRect = (k == Kind::ZoomIn || k == Kind::ZoomOut || k == Kind::PageForward || k == Kind::PageBack) &&
                            (g_pendingFrom.w <= 0 || g_pendingFrom.h <= 0);
        const bool fromHeld = g_holding;
        g_hold = g_holding = false;
        if (g_have || k == Kind::FromBlack) {
            if (!fromHeld) g_snap = 1 - g_snap;  // the frame last kept (else the held one)
            g_run.on = true;
            g_run.kind = noRect ? Kind::Fade : k;
            g_run.from = g_pendingFrom;
            g_run.start = now;
            g_run.ms = Duration(g_run.kind) * g_slow;
            g_run.t = 0;
        }
    }
    if (g_run.on) {
        g_run.t = Clamp01(ticks_to_microsecs(now - g_run.start) / 1000.0f / g_run.ms);
        Affine live, old;
        float oldAlpha, black;
        Pose(g_run, live, old, oldAlpha, black);
        Menu_SetCamera(live.k, live.tx, live.ty);
    } else {
        Menu_SetCamera(1, 0, 0);
    }
}

void FrameEnd() {
    Menu_SetCamera(1, 0, 0);
    if (g_holding) {
        DrawPicture(g_snap, Affine{}, 1);
    } else if (g_run.on) {
        Affine live, old;
        float oldAlpha, black;
        Pose(g_run, live, old, oldAlpha, black);
        const bool page = g_run.kind == Kind::PageForward || g_run.kind == Kind::PageBack;
        if (g_run.kind != Kind::FromBlack) DrawPicture(g_snap, old, oldAlpha, page ? &g_run.from : nullptr);
        if (black > 0) Menu_FillWholeScreen((GXColor){0, 0, 0, static_cast<u8>(std::lround(black * 255))});
        if (g_run.t >= 1) g_run.on = false;
    }
    Keep(1 - g_snap);
}

void SetSlowMotion(float factor) { g_slow = factor < 1 ? 1 : factor; }
float SlowMotion() { return g_slow; }

void Settle(unsigned maxMs) {
    const u64 start = gettime();
    while ((g_pending || g_hold || g_run.on) && ticks_to_millisecs(gettime() - start) < maxMs) usleep(5000);
}

bool Busy() { return g_holding || (g_run.on && g_run.t < 0.7f); }

}  // namespace riftwii::wii::transition
