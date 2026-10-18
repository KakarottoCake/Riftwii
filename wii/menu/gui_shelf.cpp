// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Home grid's shelf: the games' boxes standing spine out on a plank,
// in perspective. The focused box slides out towards you and turns to show
// its front; the boxes beside it lean apart to make room. Each box's
// corners are worked out here, once a frame: the same points are drawn,
// tested for which faces look at you, and used to find the box a pointer
// is on.
//
// Around them: a soft reflection below the plank, a shadow behind the
// focused box, the boxes further from it a little darker, a sheen on its
// sleeve, and behind it all a blurred copy of its cover. Up or Down turns
// it round to show its back; it sways a little while the focus rests. A
// held direction speeds up, and the pointer with A held drags the shelf
// and lets it glide.
//
// The same input and backdrop serve the ring (DrawRing): GameTDB's 3D
// cover pictures round a ring seen from a little above, the focused one
// large in front, with no first or last.
#include <gccore.h>
#include <ogc/lwp_watchdog.h>

#include <algorithm>
#include <cmath>
#include <string>

#include "boxart.hpp"
#include "cover3d.hpp"
#include "covers.hpp"
#include "gameextras.hpp"
#include "gui_gamegrid.hpp"
#include "i18n.hpp"
#include "loadersettings.hpp"
#include "menutheme.hpp"
#include "riftwii/casecolour.hpp"
#include "riftwii/coverart.hpp"
#include "skin.hpp"
#include "video.h"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

// World units are screen pixels on the plane z = 0, y down; the eye looks
// at that plane's centre from kEye in front, so z = 0 is drawn 1:1.
constexpr float kEye = 600.0f;
constexpr float kFovY = 43.6028f;  // 2 * atan(240 / kEye), in degrees

// A Wii case, to scale: 190 tall, 135 wide, 14 thick (mm).
constexpr float kBoxH = 164.0f;
// A Wii case's shape: a DVD keep case, 190 x 135 x 14 mm.
constexpr float kBoxW = kBoxH * 135.0f / 190.0f;  // spine to front edge, along z
constexpr float kBoxT = kBoxH * 14.0f / 190.0f;   // the spine's width
// The plank lands on the lower shelf a theme paints under Home's second
// row of covers (themes/Bookshelf): its top seen from 247 to 250, its front
// edge down to 268.
constexpr float kShelfY = 248.0f;
constexpr float kPitch = kBoxT + 2.0f;          // spine to spine on the shelf
constexpr float kOpen = kBoxW / 2.0f + 19.0f;   // the focused box's room each side
constexpr float kPullZ = 70.0f;                 // how far the focused box comes out
constexpr float kLift = 10.0f;                  // a spine under the pointer rises
constexpr int kReach = 13;                      // boxes drawn each side of the focus
constexpr float kEase = 0.25f;
constexpr int kSpineTextSize = 13;
constexpr int kSpineTextRoom = 140;  // the length of a spine's name
// The reflection: below the plank's front edge (16 tall), as if the floor
// in front of the shelf were glossy; the part of each box nearest the
// floor, fading out.
constexpr float kMirrorY = kShelfY + 16.0f;
// The carousel: the boxes on an arc of this radius, this far apart (in
// radians) and as many each side; each turned away from the middle by
// part of its angle, and lower the further out it stands.
constexpr float kArcRadius = 700.0f;
constexpr float kArcStep = 0.16f;
constexpr int kArcReach = 7;
constexpr float kArcTurn = 1.5f;
constexpr float kArcDrop = 150.0f;
// Where the reflection's mirror lies: under the plank's edge, or (the
// carousel, no plank) right under the boxes.
float g_mirrorY = kMirrorY;
// The ring (the 3D covers view): the pictures stand round a ring seen
// from a little above, close enough to overlap, the focused one large in
// front and the ring's far side small and high behind. Up to kRingPlaces
// stand round it (all the games, when there are fewer), the ring's size
// set so the front ones stand kRingSpacing apart (overlapping), but never
// narrower than kRingMinRadiusX either side; its far side stands
// kRingRise higher. kRingReach: the games either side of the focus fetched
// and kept ready.
constexpr int kRingPlaces = 20;
constexpr int kRingReach = kRingPlaces / 2;
constexpr float kRingFrontH = 236.0f;
constexpr float kRingFloor = 266.0f;
constexpr float kRingSpacing = 124.0f;
constexpr float kRingMinRadiusX = 200.0f;
constexpr float kRingRise = 150.0f;
constexpr float kRingReflect = 0.22f;  // of a picture's height, mirrored under its foot
constexpr float kReflectPart = 0.30f;
constexpr int kReflectAlpha = 54;
constexpr float kDragStep = 30.0f;  // pointer pixels a box (dragging, gliding)
constexpr float kTau = 6.2831853f;

// The texture's spine, front and back, across its kBoxWidth.
constexpr float kSpineU = static_cast<float>(riftwii::kBoxSpineWidth) / riftwii::kBoxWidth;
constexpr float kBackU = static_cast<float>(riftwii::kBoxSpineWidth + riftwii::kBoxFrontWidth) / riftwii::kBoxWidth;

struct V3 {
    float x, y, z;
};

V3 Sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

// Where a world point lands on the screen.
void Project(V3 p, float& sx, float& sy) {
    const float k = kEye / (kEye - p.z);
    sx = 320.0f + (p.x - 320.0f) * k;
    sy = 240.0f + (p.y - 240.0f) * k;
}

bool PressedPage(GuiTrigger* t, int delta) {
    const u32 wpad = delta < 0 ? (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS) : (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS);
    const u32 drc = delta < 0 ? WIIDRC_BUTTON_MINUS : WIIDRC_BUTTON_PLUS;
    return (t->wpad && (t->wpad->btns_d & wpad)) || (t->wiidrcdata.btns_d & drc);
}

// + or - held: -1, +1, or 0 for neither.
int HeldStep(GuiTrigger* t) {
    const u32 minus = WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, plus = WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS;
    const u32 held = t->wpad ? t->wpad->btns_h : 0;
    const u32 drc = t->wiidrcdata.btns_h;
    if ((held & minus) || (drc & WIIDRC_BUTTON_MINUS)) return -1;
    if ((held & plus) || (drc & WIIDRC_BUTTON_PLUS)) return 1;
    return 0;
}

bool PressedA(GuiTrigger* t) {
    return (t->wpad && (t->wpad->btns_d & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))) || (t->pad.btns_d & PAD_BUTTON_A) ||
           (t->wiidrcdata.btns_d & WIIDRC_BUTTON_A);
}

bool AnyPointer() {
    for (int i = 0; i < 4; i++)
        if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
    return false;
}

// One face of a box: four corners (clockwise as seen from outside, top
// left first), its outward normal, and what covers it.
struct Face {
    V3 c[4];
    V3 normal;
    const u8* tex = nullptr;  // RGB5A3, or RGBA8 with `rgba8`
    int texW = 0, texH = 0;
    float u0 = 0, u1 = 1;
    float v0 = 0, v1 = 1;
    bool rgba8 = false;
    // Repeats across (the plank's wood). Only for power-of-two sizes: GX
    // wraps with a power-of-two mask, so the 40x40 disc icon drawn with
    // GX_REPEAT came out as broken arcs on a Wii (Dolphin drew it whole).
    bool repeat = false;
    GXColor color;
    u8 fade[4] = {255, 255, 255, 255};  // each corner's share of the alpha (a gradient)
    // A back cover made up for a game without GameTDB's full scan: a Wii
    // case's back laid out from its front cover (`art`) and colour.
    bool madeUpBack = false;
    const u8* art = nullptr;
    GXColor accent = {0, 0, 0, 255};
    // The open edge of a keep case: rounded across its thickness.
    bool rounded = false;
};

V3 Mix(V3 a, V3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }
V3 Add(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 Scale(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float Light(V3 n) { return 0.62f + 0.38f * std::max(0.0f, (n.z * 0.85f - n.y * 0.35f - n.x * 0.25f)); }

// One quad, flat colour or textured (RGB5A3 `tex` of w x h, its part u0..u1, v0..v1).
void Quad(const V3 (&c)[4], GXColor col, const u8* fade, const u8* tex, int w, int h, float u0, float u1, float v0,
          float v1, bool rgba8, bool repeat) {
    if (tex) {
        GXTexObj obj;
        GX_InitTexObj(&obj, const_cast<u8*>(tex), static_cast<u16>(w), static_cast<u16>(h), rgba8 ? GX_TF_RGBA8 : GX_TF_RGB5A3,
                      repeat ? GX_REPEAT : GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjLOD(&obj, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
        GX_LoadTexObj(&obj, GX_TEXMAP0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    } else {
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    }
    const float us[4] = {u0, u1, u1, u0};
    const float vs[4] = {v0, v0, v1, v1};
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; ++i) {
        GX_Position3f32(c[i].x, c[i].y, c[i].z);
        GX_Color4u8(col.r, col.g, col.b, static_cast<u8>(col.a * fade[i] / 255));
        if (tex) GX_TexCoord2f32(us[i], vs[i]);
    }
    GX_End();
}

// The made-up back (Face::madeUpBack) over its paper: a grey band along
// the top as on Wii cases, the cover's middle as the big picture, two
// crops of it as screenshots, lines of text, a rating box and a barcode.
void DrawMadeUpBack(const Face& f, float light, u8 a) {
    const auto at = [&](float u, float v) { return Mix(Mix(f.c[0], f.c[1], u), Mix(f.c[3], f.c[2], u), v); };
    const auto lit = [&](GXColor c) {
        const auto sh = [&](u8 x) { return static_cast<u8>(std::min(255.0f, x * light)); };
        return GXColor{sh(c.r), sh(c.g), sh(c.b), a};
    };
    const u8 full[4] = {255, 255, 255, 255};
    const auto rect = [&](float u0, float v0, float u1, float v1, GXColor c, const u8* tex = nullptr, float tu0 = 0,
                          float tu1 = 1, float tv0 = 0, float tv1 = 1) {
        const V3 q[4] = {at(u0, v0), at(u1, v0), at(u1, v1), at(u0, v1)};
        Quad(q, lit(c), full, tex, riftwii::kCoverWidth, riftwii::kCoverHeight, tu0, tu1, tv0, tv1, false, false);
    };
    const GXColor white = {255, 255, 255, 255};
    rect(0.0f, 0.0f, 1.0f, 0.085f, {206, 208, 212, 255});  // the Wii band
    rect(0.0f, 0.085f, 1.0f, 0.095f, f.accent);
    if (f.art) {
        rect(0.08f, 0.14f, 0.92f, 0.47f, white, f.art, 0.0f, 1.0f, 0.22f, 0.62f);
        rect(0.08f, 0.51f, 0.48f, 0.66f, white, f.art, 0.05f, 0.50f, 0.58f, 0.86f);
        rect(0.52f, 0.51f, 0.92f, 0.66f, white, f.art, 0.50f, 0.95f, 0.30f, 0.58f);
    } else {
        rect(0.08f, 0.14f, 0.92f, 0.47f, f.accent);
        rect(0.08f, 0.51f, 0.48f, 0.66f, {150, 154, 160, 255});
        rect(0.52f, 0.51f, 0.92f, 0.66f, {150, 154, 160, 255});
    }
    const GXColor ink = {176, 178, 182, 255};
    rect(0.08f, 0.71f, 0.92f, 0.725f, ink);
    rect(0.08f, 0.745f, 0.92f, 0.76f, ink);
    rect(0.08f, 0.78f, 0.66f, 0.795f, ink);
    rect(0.08f, 0.85f, 0.20f, 0.95f, {30, 30, 32, 255});  // the rating
    rect(0.66f, 0.86f, 0.92f, 0.94f, white);                // the barcode
    for (int i = 0; i < 9; ++i) {
        const float u = 0.675f + i * 0.026f;
        rect(u, 0.87f, u + (i % 3 == 0 ? 0.012f : 0.006f), 0.93f, {24, 24, 24, 255});
    }
}

void DrawFace(const Face& f, int alpha) {
    // Lit from above and the left, never darker than half.
    const float light = Light(f.normal);
    const auto shade = [&](u8 c) { return static_cast<u8>(std::min(255.0f, c * light)); };
    const u8 a = static_cast<u8>(f.color.a * alpha / 255);
    if (f.rounded) {
        // Half a round rod: from the front cover's edge round to the
        // back's, bulging out by a third of the thickness.
        const V3 across = Sub(f.c[1], f.c[0]);
        const float half = std::sqrt(Dot(across, across)) / 2;
        if (half <= 0.0f) return;
        const V3 ax = Scale(across, 1.0f / (2 * half));
        const float bulge = half * 0.66f;
        const V3 midTop = Mix(f.c[0], f.c[1], 0.5f), midBottom = Mix(f.c[3], f.c[2], 0.5f);
        constexpr int kSteps = 6;
        for (int i = 0; i < kSteps; ++i) {
            const float t0 = -1.5707963f + 3.1415926f * i / kSteps, t1 = -1.5707963f + 3.1415926f * (i + 1) / kSteps;
            const auto ring = [&](V3 mid, float t) { return Add(Add(mid, Scale(ax, std::sin(t) * half)), Scale(f.normal, std::cos(t) * bulge)); };
            const V3 q[4] = {ring(midTop, t0), ring(midTop, t1), ring(midBottom, t1), ring(midBottom, t0)};
            const float tm = (t0 + t1) / 2;
            const V3 n = Add(Scale(ax, std::sin(tm)), Scale(f.normal, std::cos(tm)));
            const float l = Light(n);
            const auto sh = [&](u8 c) { return static_cast<u8>(std::min(255.0f, c * l)); };
            Quad(q, GXColor{sh(f.color.r), sh(f.color.g), sh(f.color.b), a}, f.fade, nullptr, 0, 0, 0, 1, 0, 1, false, false);
        }
        return;
    }
    Quad(f.c, GXColor{shade(f.color.r), shade(f.color.g), shade(f.color.b), a}, f.fade, f.tex, f.texW, f.texH, f.u0, f.u1,
         f.v0, f.v1, f.rgba8, f.repeat);
    if (f.madeUpBack) DrawMadeUpBack(f, light, a);
}

// A box's eight corners, standing on the shelf at (x, z), turned `turn`
// radians about its upright axis (towards you: the front comes round).
struct Box {
    V3 p[8];  // index bits: 1 = +x (front cover side), 2 = +y (bottom), 4 = +z (spine)
    V3 centre;
};

Box MakeBox(float x, float z, float turn, float lift) {
    Box b;
    const float cs = std::cos(turn), sn = std::sin(turn);
    for (int i = 0; i < 8; ++i) {
        const float lx = (i & 1) ? kBoxT / 2 : -kBoxT / 2;
        const float ly = (i & 2) ? 0.0f : -kBoxH;
        const float lz = (i & 4) ? kBoxW / 2 : -kBoxW / 2;
        b.p[i] = {x + lx * cs - lz * sn, kShelfY - lift + ly, z + lx * sn + lz * cs};
    }
    b.centre = {x, kShelfY - lift - kBoxH / 2, z};
    return b;
}

bool FacesEye(const Face& f) {
    const V3 eye = {320.0f, 240.0f, kEye};
    const V3 mid = {(f.c[0].x + f.c[2].x) / 2, (f.c[0].y + f.c[2].y) / 2, (f.c[0].z + f.c[2].z) / 2};
    return Dot(f.normal, Sub(eye, mid)) > 0.0f;
}

V3 Normal(const Box& b, V3 a, V3 c) {
    // The face through corners a..c points away from the box's centre.
    const V3 mid = {(a.x + c.x) / 2, (a.y + c.y) / 2, (a.z + c.z) / 2};
    V3 n = Sub(mid, b.centre);
    const float len = std::sqrt(Dot(n, n));
    if (len > 0) n = {n.x / len, n.y / len, n.z / len};
    return n;
}

void LoadPerspective() {
    Mtx44 p;
    guPerspective(p, kFovY, 640.0f / 480.0f, 10.0f, 3000.0f);
    Menu_ScaleProjection(p);  // widescreen and screen size, as the 2D view
    GX_LoadProjectionMtx(p, GX_PERSPECTIVE);
    // The world (y down, the eye at z = kEye) into GX's camera space.
    Mtx view = {{1, 0, 0, -320.0f}, {0, -1, 0, 240.0f}, {0, 0, 1, -kEye}};
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
}

// The case's plastic: white, unless GameTDB knows it another colour (New
// Super Mario Bros. Wii's red); a black one is lifted a little so its
// edges show.
GXColor CasePlastic(const std::string& id) {
    if (id.empty()) return {184, 205, 230, 255};  // the disc drive's box: pale blue, as its picture
    std::uint8_t rgb[3];
    if (!riftwii::game_case_colour(id, rgb)) return {238, 238, 236, 255};
    return {static_cast<u8>(28 + rgb[0] * 210 / 255), static_cast<u8>(28 + rgb[1] * 210 / 255),
            static_cast<u8>(28 + rgb[2] * 210 / 255), 255};
}

// A box's five faces you can see (not its bottom), covered with its scan,
// its cover or plain white. Spine, front, back, top, open edge; the top
// and the open edge are the case's own plastic, in its colour (a scan is
// drawn through its face's colour, so the paper faces stay white).
void BoxFaces(const std::string& id, const Box& b, const u8* art, const u8* cover, GXColor hue, Face (&faces)[5]) {
    const GXColor plastic = {238, 238, 236, 255};
    // Corners by bits: x (1) y (2) z (4).
    const auto face = [&](int a, int bb, int c, int d) {
        Face f;
        f.c[0] = b.p[a];
        f.c[1] = b.p[bb];
        f.c[2] = b.p[c];
        f.c[3] = b.p[d];
        f.normal = Normal(b, b.p[a], b.p[c]);
        f.color = plastic;
        return f;
    };
    // The spine (+z): left edge at -x as you face it.
    faces[0] = face(4, 5, 7, 6);
    // The front cover (+x): seen from +x, the spine's edge (+z) on the left.
    faces[1] = face(5, 1, 3, 7);
    // The back cover (-x): the spine on the right.
    faces[2] = face(0, 4, 6, 2);
    // The top (-y) and the open edge (-z).
    faces[3] = face(0, 1, 5, 4);
    faces[4] = face(1, 0, 2, 3);
    faces[3].color = faces[4].color = CasePlastic(id);
    faces[4].rounded = true;
    if (art) {
        faces[0].tex = art;
        faces[0].texW = riftwii::kBoxWidth;
        faces[0].texH = riftwii::kBoxHeight;
        faces[0].u0 = 0;
        faces[0].u1 = kSpineU;
        faces[1].tex = art;
        faces[1].texW = riftwii::kBoxWidth;
        faces[1].texH = riftwii::kBoxHeight;
        faces[1].u0 = kSpineU;
        faces[1].u1 = kBackU;
        // The back cover, its spine edge (+z) on the right as you face it.
        faces[2].tex = art;
        faces[2].texW = riftwii::kBoxWidth;
        faces[2].texH = riftwii::kBoxHeight;
        faces[2].u0 = kBackU;
        faces[2].u1 = 1;
    } else if (cover) {
        faces[1].tex = cover;
        faces[1].texW = riftwii::kCoverWidth;
        faces[1].texH = riftwii::kCoverHeight;
    } else if (id.empty() && skin::discCase.data) {
        // The disc drive: its own box on the front and the back, the spine
        // in its pale blue.
        for (const int k : {1, 2}) {
            faces[k].tex = skin::discCase.data;
            faces[k].texW = skin::discCase.w;
            faces[k].texH = skin::discCase.h;
            faces[k].rgba8 = true;
        }
        faces[0].color = {214, 228, 244, 255};
        return;
    }
    // No scan: a back made up from the front cover, laid out as a Wii
    // case's back is.
    if (!art) {
        faces[2].color = {244, 244, 242, 255};
        faces[2].madeUpBack = true;
        faces[2].art = cover;
        faces[2].accent = {hue.r, hue.g, hue.b, 255};
    }
}

// The game whose box is flying (DrawShelfFlight): the shelf leaves its
// place empty until it lands.
std::string g_flying;

// Back to libgui's 2D view (vendor-libgui video.cpp).
void LoadFlat() {
    Menu_LoadOrtho();
    Mtx view;
    guMtxIdentity(view);
    guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
}

V3 Lerp(V3 a, V3 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t}; }

void Dim(Face& f, float k) {
    f.color.r = static_cast<u8>(f.color.r * k);
    f.color.g = static_cast<u8>(f.color.g * k);
    f.color.b = static_cast<u8>(f.color.b * k);
}

// The upright faces' lower part, mirrored under the plank and fading out
// downwards. `k` darkens it with its box.
void DrawReflection(const Face (&faces)[5], float k, int alpha) {
    const auto mirror = [](V3 p) { return V3{p.x, g_mirrorY + (kShelfY - p.y), p.z}; };
    for (const int i : {0, 1, 2, 4}) {
        const Face& f = faces[i];
        Face r = f;
        // Corners: 0, 1 the top edge, 3, 2 the bottom (on the plank).
        r.c[0] = mirror(f.c[3]);
        r.c[1] = mirror(f.c[2]);
        r.c[2] = mirror(Lerp(f.c[2], f.c[1], kReflectPart));
        r.c[3] = mirror(Lerp(f.c[3], f.c[0], kReflectPart));
        r.normal = {f.normal.x, -f.normal.y, f.normal.z};
        if (!FacesEye(r)) continue;
        r.v0 = f.v1;
        r.v1 = f.v1 - (f.v1 - f.v0) * kReflectPart;
        r.fade[0] = r.fade[1] = 255;
        r.fade[2] = r.fade[3] = 0;
        r.madeUpBack = false;
        Dim(r, k);
        r.color.a = static_cast<u8>(r.color.a * kReflectAlpha / 255);
        DrawFace(r, alpha);
    }
}

// The focused box's soft shadow on the wall behind the shelf: a few
// growing, faint copies of its outline, down and to the right.
void DrawWallShadow(const Box& b, float strength, int alpha) {
    if (strength <= 0.02f) return;
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (const V3& p : b.p) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
    }
    const float z = -kBoxW / 2 - 8, dx = 30, dy = 16;
    for (int layer = 0; layer < 4; ++layer) {
        const float g = 2.5f * layer;
        Face s;
        s.c[0] = {x0 + dx - g, y0 + dy - g, z};
        s.c[1] = {x1 + dx + g, y0 + dy - g, z};
        s.c[2] = {x1 + dx + g, y1 + dy + g, z};
        s.c[3] = {x0 + dx - g, y1 + dy + g, z};
        s.normal = {0, 0, 1};
        s.color = {0, 0, 0, static_cast<u8>(26 * strength)};
        DrawFace(s, alpha);
    }
}

// The clear sleeve over a cover catching the light: brightest at the top
// corner on the spine's side, gone by the far corner; `phase` (the sway)
// moves it a little.
void DrawSheen(const Face& f, float phase, int alpha) {
    if (!FacesEye(f)) return;
    Face g = f;
    for (V3& c : g.c) {
        c.x += f.normal.x * 0.5f;
        c.y += f.normal.y * 0.5f;
        c.z += f.normal.z * 0.5f;
    }
    g.tex = nullptr;
    g.color = {255, 255, 255, 62};
    const float m = 0.75f + 0.25f * phase;
    g.fade[0] = static_cast<u8>(255 * m);
    g.fade[1] = static_cast<u8>(70 * m);
    g.fade[2] = 0;
    g.fade[3] = static_cast<u8>(70 * m);
    DrawFace(g, alpha);
}

// The backdrop: a blurred copy of the focused game's cover across the
// screen, far behind the shelf, the last one fading out as the next one
// fades in.
alignas(32) u8 g_backPx[2][riftwii::kBackdropBytes];
std::string g_backId[2];
float g_backShown[2] = {0, 0};
int g_backCur = 0;

// `id` "*disc": the disc drive's box (skin::discCase).
constexpr const char* kDiscBackdrop = "*disc";

void UpdateBackdrop(const std::string& id, bool still) {
    if (g_backId[g_backCur] != id && still && !id.empty()) {
        const bool disc = id == kDiscBackdrop;
        const u8* art = disc ? nullptr : riftwii::wii::BoxTexture(id);
        const u8* cover = art || disc ? nullptr : riftwii::wii::CoverTexture(id);
        if (art || cover || (disc && skin::discCase.data)) {
            const int next = 1 - g_backCur;
            if (disc)
                riftwii::make_backdrop(skin::discCase.data, skin::discCase.w, skin::discCase.h, 0, skin::discCase.w,
                                       g_backPx[next], true);
            else if (art)
                riftwii::make_backdrop(art, riftwii::kBoxWidth, riftwii::kBoxHeight, riftwii::kBoxSpineWidth,
                                       riftwii::kBoxSpineWidth + riftwii::kBoxFrontWidth, g_backPx[next]);
            else
                riftwii::make_backdrop(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, 0, riftwii::kCoverWidth,
                                       g_backPx[next]);
            DCFlushRange(g_backPx[next], sizeof(g_backPx[next]));
            GX_InvalidateTexAll();
            g_backId[next] = id;
            g_backShown[next] = 0;
            g_backCur = next;
        }
    }
    for (int k = 0; k < 2; ++k) {
        const float target = k == g_backCur && g_backId[k] == id ? 1.0f : 0.0f;
        g_backShown[k] += (target - g_backShown[k]) * 0.08f;
    }
}

void DrawBackdrop(int alpha) {
    if (riftwii::wii::MenuTheme().clock_dials) return;  // the clock faces are the point
    f32 vx, vy, vw, vh;
    Menu_VisibleArea(&vx, &vy, &vw, &vh);
    // At z = -kEye a world unit is half a screen pixel.
    const float z = -kEye, k = 0.5f;
    const float sx0 = vx - 8, sx1 = vx + vw + 8, sy0 = vy - 8, sy1 = vy + vh + 8;
    // It stops above the bottom bar, fading out over its last stretch, so
    // the clock in the bar's dip stays crisp.
    const float fadeTop = 300, fadeEnd = 348;
    const auto wx = [&](float sx) { return 320.0f + (sx - 320.0f) / k; };
    const auto wy = [&](float sy) { return 240.0f + (sy - 240.0f) / k; };
    // The picture's whole width across the screen; as much of its height
    // as that leaves, from near its top (where a cover's title is).
    const float aspect = (sx1 - sx0) / (sy1 - sy0);
    const float span = std::min(1.0f, (static_cast<float>(riftwii::kBackdropWidth) / riftwii::kBackdropHeight) / aspect);
    const float v0 = std::min(0.12f, 1.0f - span);
    const auto vAt = [&](float sy) { return v0 + span * (sy - sy0) / (sy1 - sy0); };
    for (const int which : {1 - g_backCur, g_backCur}) {
        if (g_backShown[which] < 0.01f || g_backId[which].empty()) continue;
        // Two bands: whole down to fadeTop, then fading to nothing at fadeEnd.
        for (const int band : {0, 1}) {
            const float ya = band == 0 ? sy0 : fadeTop, yb = band == 0 ? fadeTop : fadeEnd;
            Face f;
            f.c[0] = {wx(sx0), wy(ya), z};
            f.c[1] = {wx(sx1), wy(ya), z};
            f.c[2] = {wx(sx1), wy(yb), z};
            f.c[3] = {wx(sx0), wy(yb), z};
            f.normal = {0, 0, 1};
            f.tex = g_backPx[which];
            f.texW = riftwii::kBackdropWidth;
            f.texH = riftwii::kBackdropHeight;
            f.v0 = vAt(ya);
            f.v1 = vAt(yb);
            f.color = {150, 150, 156, static_cast<u8>(150 * g_backShown[which])};
            if (band == 1) f.fade[2] = f.fade[3] = 0;
            DrawFace(f, alpha);
        }
    }
}

// Under the focused game's name: where it is, its region, how often it
// was played, whether it has mods.
std::string ShelfInfo(const GridItem& item) {
    using riftwii::wii::tr;
    std::string out;
    const auto add = [&](const std::string& part) {
        if (part.empty()) return;
        if (!out.empty()) out += "   \u00b7   ";
        out += part;
    };
    if (item.badge == "USB") add(tr("USB drive"));
    else if (item.badge == "SD") add(tr("SD card"));
    else if (item.badge == "GC") add(tr("GameCube"));
    if (item.id.size() >= 4) {
        switch (item.id[3]) {
            case 'E': add(tr("USA")); break;
            case 'P': case 'X': case 'Y': case 'Z': add(tr("Europe")); break;
            case 'J': add(tr("Japan")); break;
            case 'K': add(tr("Korea")); break;
            case 'W': add(tr("Taiwan")); break;
            default: break;
        }
        // Play history off (Settings): no count, as the game page shows none.
        const riftwii::PlayRecord* played = riftwii::wii::History().find(item.id);
        const unsigned count = played ? played->count : 0;
        if (riftwii::wii::Settings().play_history != "off")
            add(count == 0   ? std::string(tr("Not played yet"))
                : count == 1 ? std::string(tr("Played once"))
                             : tr("Played {1} times", {std::to_string(count)}));
    }
    if (item.to_build) add(tr("Not made yet"));
    else if (item.mods) add(tr("Mods"));
    return out;
}

}  // namespace

std::vector<int> GuiGameGrid::ShelfWanted() const {
    std::vector<int> out;
    if (!shelf) return out;
    if (ring) {
        // Round the ring both ways from the focus, each game once.
        const int n = Count();
        for (int d = 0; d <= kRingReach && static_cast<int>(out.size()) < n; ++d) {
            for (const int k : {focus + d, focus - d}) {
                const int i = ((k % n) + n) % n;
                if (std::find(out.begin(), out.end(), i) == out.end()) out.push_back(i);
            }
        }
        return out;
    }
    for (int d = 0; d <= kReach; ++d) {
        if (focus + d < Count()) out.push_back(focus + d);
        if (d > 0 && focus - d >= 0) out.push_back(focus - d);
    }
    return out;
}

void GuiGameGrid::BoxArrived(const std::string& id) { riftwii::wii::ForgetBox(id); }
void GuiGameGrid::Cover3dArrived(const std::string& id) { riftwii::wii::ForgetCover3d(id); }

void GuiGameGrid::ShelfStep(int delta) {
    // The ring has no ends: past the last game comes the first.
    const int n = Count();
    if (n <= 0) return;
    const int target = ring ? ((focus + delta) % n + n) % n : std::clamp(focus + delta, 0, n - 1);
    if (target == focus) return;
    focus = target;
    page = focus / kPerPage;
    soundOver->Play();
}

int GuiGameGrid::ShelfHit(int x, int y) const {
    // The nearest drawn box first: they were stored back to front.
    for (auto it = spineRects.rbegin(); it != spineRects.rend(); ++it)
        if (x >= it->x0 && x < it->x1 && y >= it->y0 && y < it->y1) return it->index;
    // A box pointed at rises (kLift, 8 px on the ring), so a pointer at its
    // foot was below it the next frame: it fell, was pointed at again and
    // rose, over and over, which shook it (a tester's photo, the ring). The
    // box pointed at keeps the pointer as far below it as it rose.
    constexpr float kRiseSlack = 14.0f;
    if (shelfHover >= 0)
        for (const SpineRect& r : spineRects)
            if (r.index == shelfHover && x >= r.x0 && x < r.x1 && y >= r.y1 && y < r.y1 + kRiseSlack) return r.index;
    return -1;
}

void GuiGameGrid::DrawShelf(int alpha) {
    if (ring) {
        DrawRing(alpha);
        return;
    }
    if (static_cast<int>(poses.size()) != Count()) poses.assign(Count(), BoxPose{});
    ++shelfClock;
    if (shelfFlipFor != focus) {
        shelfFlipFor = focus;
        shelfFlip = false;
        shelfStill = 0;
    } else if (shelfStill < 100000) {
        ++shelfStill;
    }
    // A flick's glide: steps while it slows down.
    if (!dragging && glide != 0.0f) {
        glideAccum += glide;
        glide *= 0.88f;
        while (glideAccum >= kDragStep) {
            ShelfStep(-1);
            glideAccum -= kDragStep;
        }
        while (glideAccum <= -kDragStep) {
            ShelfStep(1);
            glideAccum += kDragStep;
        }
        if (std::fabs(glide) < 0.8f) glide = glideAccum = 0.0f;
    }
    // The sway fades in once the focus has rested a moment.
    const float settle = std::clamp((shelfStill - 40) / 60.0f, 0.0f, 1.0f);
    const float swing = std::sin(shelfClock * 0.03f);
    // Enough boxes each side to reach past the screen's edges (a widescreen
    // menu's are further out), so they come and go off screen: at kReach
    // they appeared and vanished in view on a 16:9 menu.
    f32 vx, vy, vw, vh;
    Menu_VisibleArea(&vx, &vy, &vw, &vh);
    const int reach = carousel ? kArcReach
                               : std::max(kReach, static_cast<int>((320.0f - vx + 60.0f - kOpen - kBoxT / 2) / kPitch) + 2);
    g_mirrorY = carousel ? kShelfY + 2.0f : kMirrorY;
    const int first = std::max(0, focus - reach), last = std::min(Count() - 1, focus + reach);

    // Where each box near the focus wants to be, and a step towards it.
    // From the focus outwards, so a box's neighbour nearer the focus has
    // its place for this frame already.
    std::vector<int>& outward = shelfOutward;
    outward.clear();
    for (int i = focus; i <= last; ++i) outward.push_back(i);
    for (int i = focus - 1; i >= first; --i) outward.push_back(i);
    for (const int i : outward) {
        const int d = i - focus;
        const int ad = d < 0 ? -d : d;
        BoxPose& p = poses[i];
        BoxPose want;
        if (carousel) {
            // On the arc: further round and further back the further from
            // the focus; facing out, turned partly away from the middle.
            const float a = d * kArcStep;
            want.x = 320.0f + kArcRadius * std::sin(a);
            want.z = (d == 0 ? kPullZ : 0.0f) + kArcRadius * (std::cos(a) - 1.0f);
            want.turn = static_cast<float>(M_PI) / 2 - kArcTurn * a + (d == 0 ? 0.07f * swing * settle : 0.0f) +
                        (d == 0 && shelfFlip ? static_cast<float>(M_PI) : 0.0f);
            want.lift = -kArcDrop * (1.0f - std::cos(a)) + (i == shelfHover ? kLift : 0.0f) +
                        (d == 0 ? 3.0f * (0.5f + 0.5f * std::sin(shelfClock * 0.045f)) * settle : 0.0f);
            if (!p.placed) {
                p = want;
                p.placed = true;
                continue;
            }
        } else if (d == 0) {
            want.x = 320.0f;
            want.z = kPullZ;
            want.turn = static_cast<float>(M_PI) / 2 + (shelfFlip ? static_cast<float>(M_PI) : 0.0f) + 0.07f * swing * settle;
            want.lift = 3.0f * (0.5f + 0.5f * std::sin(shelfClock * 0.045f)) * settle;
        } else {
            const float off = kOpen + kBoxT / 2 + (ad - 1) * kPitch;
            want.x = 320.0f + (d < 0 ? -off : off);
            // Turned past a half (its back was shown): on round, not back.
            want.turn = p.placed && p.turn > static_cast<float>(M_PI) ? kTau : 0.0f;
            want.lift = i == shelfHover ? kLift : 0.0f;
        }
        if (!p.placed) {
            // Coming into reach: a spine beside its neighbour nearer the
            // focus, wherever that one is now, so the row moves as one (put
            // straight at its place it overlapped the boxes still sliding).
            const int n = d < 0 ? i + 1 : i - 1;
            if (d != 0 && n != focus && n >= first && n <= last && poses[n].placed) {
                p = BoxPose{};
                p.x = poses[n].x + (d < 0 ? -kPitch : kPitch);
            } else {
                p = want;
            }
            p.placed = true;
            continue;
        }
        p.x += (want.x - p.x) * kEase;
        p.z += (want.z - p.z) * kEase;
        p.turn += (want.turn - p.turn) * kEase;
        p.lift += (want.lift - p.lift) * kEase;
        if (want.turn == kTau && p.turn > kTau - 0.01f) p.turn = 0.0f;
    }
    // Boxes that fell out of reach start again from their place next time.
    for (int i = 0; i < Count(); ++i)
        if (i < first || i > last) poses[i].placed = false;

    // Back to front: the farthest from the middle first, the focused one last.
    std::vector<int>& order = shelfOrder;
    order.clear();
    for (int i = first; i <= last; ++i) order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const float da = std::fabs(poses[a].x - 320.0f) - poses[a].z, db = std::fabs(poses[b].x - 320.0f) - poses[b].z;
        return da > db;
    });

    // How much darker a box is the further it stands from the focus.
    const auto dimFor = [&](int i) {
        const int ad = i < focus ? focus - i : i - focus;
        return (1.0f - 0.045f * std::min(ad, 7)) * (ad == 1 ? 0.93f : 1.0f);
    };
    LoadPerspective();
    const std::string backdrop = focus < 0 || focus >= Count()      ? std::string()
                                 : (*items)[focus].badge == "DISC" ? std::string(kDiscBackdrop)
                                                                   : (*items)[focus].id;
    UpdateBackdrop(backdrop, shelfStill >= 8);
    DrawBackdrop(alpha);
    if (!carousel && focus >= first && focus <= last && poses[focus].placed &&
        (g_flying.empty() || (*items)[focus].id != g_flying)) {
        const BoxPose& p = poses[focus];
        DrawWallShadow(MakeBox(p.x, p.z, p.turn, p.lift), std::fabs(std::sin(p.turn)), alpha);
    }
    for (const int i : order) {
        const GridItem& item = (*items)[i];
        if (!g_flying.empty() && item.id == g_flying) continue;
        const BoxPose& p = poses[i];
        const u8* art = riftwii::wii::BoxTexture(item.id);
        Face faces[5];
        BoxFaces(item.id, MakeBox(p.x, p.z, p.turn, p.lift), art, art ? nullptr : riftwii::wii::CoverTexture(item.id),
                 item.hue, faces);
        DrawReflection(faces, dimFor(i), alpha);
    }
    // The plank: its top, then its front edge, wider than the screen (none
    // under the carousel).
    if (!carousel) {
        const bool pic = skin::shelfPlank.data != nullptr;
        const GXColor wood = pic ? GXColor{255, 255, 255, 255} : skin::kShelfWood;
        const GXColor edgeWood = pic ? GXColor{255, 255, 255, 255} : skin::kShelfEdge;
        const auto textured = [&](Face& f, float v0, float v1) {
            if (!pic) return;
            f.tex = skin::shelfPlank.data;
            f.texW = skin::shelfPlank.w;
            f.texH = skin::shelfPlank.h;
            f.rgba8 = true;
            f.repeat = true;  // 256x64 (src/art/theme.cpp)
            // The picture's width covers 256 units of the plank.
            f.u0 = -400.0f / 256.0f;
            f.u1 = 1040.0f / 256.0f;
            f.v0 = v0;
            f.v1 = v1;
        };
        const float zb = -kBoxW / 2 - 12, zf = kPullZ + kBoxT / 2 + 14;
        Face top;
        top.c[0] = {-400, kShelfY, zb};
        top.c[1] = {1040, kShelfY, zb};
        top.c[2] = {1040, kShelfY, zf};
        top.c[3] = {-400, kShelfY, zf};
        top.normal = {0, -1, 0};
        top.color = wood;
        textured(top, 0.0f, 48.0f / 64);
        DrawFace(top, alpha);
        Face edge;
        edge.c[0] = {-400, kShelfY, zf};
        edge.c[1] = {1040, kShelfY, zf};
        edge.c[2] = {1040, kShelfY + 16, zf};
        edge.c[3] = {-400, kShelfY + 16, zf};
        edge.normal = {0, 0, 1};
        edge.color = edgeWood;
        textured(edge, 48.0f / 64, 1.0f);
        DrawFace(edge, alpha);
    }

    // The card gives one box a frame: the ones nearest the focus first
    // (drawn back to front, the farthest would have it), the rest of this
    // frame's boxes then come from the slots.
    for (int d = 0; d <= reach; ++d) {
        if (focus + d <= last) riftwii::wii::BoxTexture((*items)[focus + d].id);
        if (d > 0 && focus - d >= first) riftwii::wii::BoxTexture((*items)[focus - d].id);
    }

    spineRects.clear();
    std::vector<SpineLabel>& labels = spineLabels;
    labels.clear();
    for (const int i : order) {
        const GridItem& item = (*items)[i];
        const BoxPose& p = poses[i];
        const Box b = MakeBox(p.x, p.z, p.turn, p.lift);
        const u8* art = riftwii::wii::BoxTexture(item.id);
        const u8* cover = art ? nullptr : riftwii::wii::CoverTexture(item.id);
        // In flight to or from a game's page: drawn by DrawShelfFlight.
        if (!g_flying.empty() && item.id == g_flying) continue;
        Face faces[5];
        BoxFaces(item.id, b, art, cover, item.hue, faces);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        bool spineShown = false;
        const float k = dimFor(i);
        if (k < 1.0f)
            for (Face& f : faces) Dim(f, k);
        for (const Face& f : faces) {
            if (!FacesEye(f)) continue;
            DrawFace(f, alpha);
            if (&f == &faces[0]) spineShown = true;
            for (const V3& c : f.c) {
                float sx, sy;
                Project(c, sx, sy);
                x0 = std::min(x0, sx);
                y0 = std::min(y0, sy);
                x1 = std::max(x1, sx);
                y1 = std::max(y1, sy);
            }
        }
        if (!art && spineShown) {
            // A plain spine: the game's colour in a band along it.
            Face band = faces[0];
            const auto along = [&](V3 top, V3 bottom, float t) {
                return V3{top.x + (bottom.x - top.x) * t, top.y + (bottom.y - top.y) * t, top.z + (bottom.z - top.z) * t};
            };
            const V3 tl = faces[0].c[0], tr = faces[0].c[1], br = faces[0].c[2], bl = faces[0].c[3];
            band.c[0] = along(tl, bl, 0.80f);
            band.c[1] = along(tr, br, 0.80f);
            band.c[2] = along(tr, br, 0.93f);
            band.c[3] = along(tl, bl, 0.93f);
            band.color = item.badge == "DISC" ? GXColor{52, 108, 196, 255} : item.hue;
            band.color.a = 255;
            band.tex = nullptr;
            DrawFace(band, alpha);
            float sx, sy0, sy1, sxr, dummy;
            Project(tl, sx, sy0);
            Project(tr, sxr, dummy);
            Project(along(tl, bl, 0.78f), dummy, sy1);
            // Not while the box still turns (to or from the focus): its name
            // lay across the next spine's.
            if (p.turn < 0.12f) labels.push_back(SpineLabel{i, (sx + sxr) / 2, sy0, sy1});
        }
        if (item.badge == "DISC" && !art && !cover && !skin::discCase.data && FacesEye(faces[1]) && skin::iconDisc.data) {
            // The disc drive has no box art: the Disc drive tile's disc
            // (as on the grid), square in the middle of the front.
            const Face& fr = faces[1];
            const auto at = [&](float u, float v) {
                const V3 t{fr.c[0].x + (fr.c[1].x - fr.c[0].x) * u, fr.c[0].y + (fr.c[1].y - fr.c[0].y) * u,
                           fr.c[0].z + (fr.c[1].z - fr.c[0].z) * u};
                const V3 b{fr.c[3].x + (fr.c[2].x - fr.c[3].x) * u, fr.c[3].y + (fr.c[2].y - fr.c[3].y) * u,
                           fr.c[3].z + (fr.c[2].z - fr.c[3].z) * u};
                return V3{t.x + (b.x - t.x) * v, t.y + (b.y - t.y) * v, t.z + (b.z - t.z) * v};
            };
            const auto len = [](V3 a, V3 b) {
                return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
            };
            const float wide = len(fr.c[0], fr.c[1]), tall = len(fr.c[0], fr.c[3]);
            const float su = 0.6f, sv = tall > 0 ? su * wide / tall : su;
            const float v0 = 0.42f - sv / 2;
            Face disc = fr;
            disc.c[0] = at(0.2f, v0);
            disc.c[1] = at(0.8f, v0);
            disc.c[2] = at(0.8f, v0 + sv);
            disc.c[3] = at(0.2f, v0 + sv);
            // A little in front of the cover, so the depth test keeps it.
            for (V3& c : disc.c) {
                c.x += fr.normal.x * 0.6f;
                c.y += fr.normal.y * 0.6f;
                c.z += fr.normal.z * 0.6f;
            }
            disc.tex = skin::iconDisc.data;
            disc.texW = skin::iconDisc.w;
            disc.texH = skin::iconDisc.h;
            disc.rgba8 = true;
            disc.u0 = disc.v0 = 0;
            disc.u1 = disc.v1 = 1;
            disc.color = {255, 255, 255, 255};
            DrawFace(disc, alpha);
        }
        if (i == focus && (art || cover || (item.badge == "DISC" && skin::discCase.data))) {
            DrawSheen(faces[1], swing * settle, alpha);
            if (art) DrawSheen(faces[2], swing * settle, alpha);
        }
        if (x1 > x0) spineRects.push_back({i, x0, y0, x1, y1});
    }
    LoadFlat();

    // The names down the plain spines, read top to bottom as on a Wii case
    // (Nintendo's spines in America and Europe).
    if (static_cast<int>(spineFit.size()) != Count()) spineFit.assign(Count(), {});
    for (const SpineLabel& l : labels) {
        auto& fit = spineFit[l.index];
        const std::string& title = (*items)[l.index].title;
        if (fit.first != title || fit.second.empty()) fit = {title, Fit(title, kSpineTextRoom)};
        spineText->SetText(fit.second.c_str());
        spineText->SetFontSize(kSpineTextSize);
        spineText->SetAlpha(alpha);
        const float w = static_cast<float>(spineText->GetTextWidth());
        // Turned a quarter right about the label's own start, at the top:
        // the line runs down and the letters' tops face right, so the
        // text's height lies left of px.
        const float px = l.x + kSpineTextSize / 2.0f + 1, py = l.y0 + 4;
        // The text sits at whole pixels (GuiText) and is turned about a
        // whole pixel, then moved by what is left after the turn: rounded
        // before it, a sliding spine's fraction of a pixel went into the
        // text's place along the spine, and the names bobbed up and down
        // as the row moved.
        const float along = std::max(0.0f, (l.y1 - l.y0 - 8 - w) / 2);
        const int ix = static_cast<int>(std::floor(px)), iy = static_cast<int>(std::floor(py));
        Mtx view, rot, m, mv;
        guMtxIdentity(view);
        guMtxTransApply(view, view, 0.0f, 0.0f, -50.0f);
        guMtxIdentity(m);
        guMtxTransApply(m, m, static_cast<float>(-ix), static_cast<float>(-iy), 0);
        guMtxRotDeg(rot, 'z', 90.0f);
        guMtxConcat(rot, m, m);
        guMtxTransApply(m, m, px, py + along, 0);
        guMtxConcat(view, m, mv);
        GX_LoadPosMtxImm(mv, GX_PNMTX0);
        spineText->SetPosition(ix, iy);
        spineText->Draw();
        GX_LoadPosMtxImm(view, GX_PNMTX0);
    }

    DrawShelfCaption(alpha);
}

// The focused (or pointed at) game's name under the shelf or the ring, and
// under it where the game is, its region, how often it was played.
void GuiGameGrid::DrawShelfCaption(int alpha) {
    const int shown = shelfHover >= 0 ? shelfHover : focus;
    if (shown != captionFor) {
        captionFor = shown;
        measure->SetFontSize(18);
        caption->SetText(shown >= 0 && shown < Count() ? Fit((*items)[shown].title, 560).c_str() : "");
        measure->SetFontSize(13);
    }
    caption->Draw();
    if (shown != infoFor) {
        infoFor = shown;
        captionInfo->SetText(shown >= 0 && shown < Count() ? ShelfInfo((*items)[shown]).c_str() : "");
    }
    captionInfo->SetAlpha(alpha);
    captionInfo->Draw();
}

void GuiGameGrid::DrawRing(int alpha) {
    const int n = Count();
    if (n <= 0) return;
    ++shelfClock;
    if (shelfFlipFor != focus) {
        shelfFlipFor = focus;
        shelfStill = 0;
    } else if (shelfStill < 100000) {
        ++shelfStill;
    }
    // A flick's glide, as on the shelf.
    if (!dragging && glide != 0.0f) {
        glideAccum += glide;
        glide *= 0.88f;
        while (glideAccum >= kDragStep) {
            ShelfStep(-1);
            glideAccum -= kDragStep;
        }
        while (glideAccum <= -kDragStep) {
            ShelfStep(1);
            glideAccum += kDragStep;
        }
        if (std::fabs(glide) < 0.8f) glide = glideAccum = 0.0f;
    }
    // The ring turns towards the focus the short way round.
    if (!ringPlaced) {
        ringPos = static_cast<float>(focus);
        ringPlaced = true;
    }
    float ahead = focus - ringPos;
    while (ahead > n / 2.0f) ahead -= n;
    while (ahead < -n / 2.0f) ahead += n;
    ringPos += std::fabs(ahead) < 0.002f ? ahead : ahead * 0.2f;
    while (ringPos >= n) ringPos -= n;
    while (ringPos < 0) ringPos += n;

    LoadPerspective();
    const std::string backdrop = (*items)[focus].badge == "DISC" ? std::string(kDiscBackdrop) : (*items)[focus].id;
    UpdateBackdrop(backdrop, shelfStill >= 8);
    DrawBackdrop(alpha);
    LoadFlat();

    // Each game once, at its angle round the ring: a whole ring of
    // kRingPlaces places, or of as many as there are games.
    const int places = std::min(n, kRingPlaces);
    const float step = kTau / places;
    const float radiusX = std::max(kRingMinRadiusX, kRingSpacing / std::sin(std::min(step, 1.2f)));
    const int base = static_cast<int>(std::floor(ringPos));
    const float frac = ringPos - base;
    struct Placed {
        int index;
        float angle;
    };
    std::vector<Placed> placed;
    const int behind = places / 2;
    for (int k = -behind; k <= places - behind; ++k) {
        const int index = ((base + k) % n + n) % n;
        const float angle = (k - frac) * step;
        if (std::fabs(angle) > static_cast<float>(M_PI) + 0.01f) continue;
        bool seen = false;
        for (Placed& p : placed) {
            if (p.index != index) continue;
            seen = true;
            if (std::fabs(angle) < std::fabs(p.angle)) p.angle = angle;
        }
        if (!seen) placed.push_back({index, angle});
    }
    // Back to front.
    std::sort(placed.begin(), placed.end(), [](const Placed& x, const Placed& y) { return std::cos(x.angle) < std::cos(y.angle); });

    const float settle = std::clamp((shelfStill - 40) / 60.0f, 0.0f, 1.0f);
    // The card gives one picture a frame: the ones nearest the focus first.
    for (const int i : ShelfWanted()) riftwii::wii::Cover3dTexture((*items)[i].id);
    spineRects.clear();
    for (const Placed& p : placed) {
        const GridItem& item = (*items)[p.index];
        // 1 in front, 0 at the back: smaller, higher and darker behind.
        const float near = 0.5f + 0.5f * std::cos(p.angle);
        // The focused picture at full size; the rest at most 72% of it,
        // smaller further round, so the ring's far side shows over them.
        const float others = 0.30f + 0.42f * std::pow(near, 1.6f);
        const float front = std::max(0.0f, 1.0f - std::fabs(p.angle) / step);
        const float scale = others + (1.0f - others) * front;
        const float h = kRingFrontH * scale;
        const float cx = 320.0f + radiusX * std::sin(p.angle);
        float foot = kRingFloor - kRingRise * (1.0f - near);
        if (p.index == shelfHover) foot -= 8.0f;
        if (p.index == focus) foot -= 3.0f * (0.5f + 0.5f * std::sin(shelfClock * 0.045f)) * settle;
        const u8 shade = static_cast<u8>(255 * (0.36f + 0.64f * near));
        const GXColor lit = {shade, shade, shade, static_cast<u8>(alpha)};
        const u8 glint = static_cast<u8>(70 * near * alpha / 255);
        const GXColor mirrorTop = {shade, shade, shade, glint}, mirrorEnd = {shade, shade, shade, 0};
        const u8* tex = riftwii::wii::Cover3dTexture(item.id);
        const u8* cover = tex ? nullptr : riftwii::wii::CoverTexture(item.id);
        float w, ph;
        if (tex) {
            ph = h;
            w = h * riftwii::kCover3dWidth / riftwii::kCover3dHeight;
            skin::DrawRgb5a3Region(tex, riftwii::kCover3dWidth, riftwii::kCover3dHeight, 0, 0, 1, 1, cx - w / 2, foot - ph,
                                   w, ph, lit, lit);
            // The reflection: the picture's foot mirrored under it, fading out.
            skin::DrawRgb5a3Region(tex, riftwii::kCover3dWidth, riftwii::kCover3dHeight, 0, 1, 1, 1 - kRingReflect,
                                   cx - w / 2, foot, w, ph * kRingReflect, mirrorTop, mirrorEnd);
        } else if (cover) {
            // No 3D cover (yet): the flat cover, as tall as a case in one.
            ph = h * 0.92f;
            w = ph * riftwii::kCoverWidth / riftwii::kCoverHeight;
            skin::DrawRgb5a3Region(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, 0, 0, 1, 1, cx - w / 2, foot - ph,
                                   w, ph, lit, lit);
            skin::DrawRgb5a3Region(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, 0, 1, 1, 1 - kRingReflect,
                                   cx - w / 2, foot, w, ph * kRingReflect, mirrorTop, mirrorEnd);
        } else if (item.badge == "DISC" && skin::discCase.data) {
            ph = h * 0.92f;
            const float k = ph / skin::discCase.h;
            w = skin::discCase.w * k;
            skin::Draw(skin::discCase, cx - w / 2, foot - ph, alpha * shade / 255, k);
        } else {
            // Nothing to show yet: the game's colour where its case goes.
            ph = h * 0.92f;
            w = ph * riftwii::kCoverWidth / riftwii::kCoverHeight;
            GXColor hue = item.hue;
            hue.r = static_cast<u8>(hue.r * shade / 255);
            hue.g = static_cast<u8>(hue.g * shade / 255);
            hue.b = static_cast<u8>(hue.b * shade / 255);
            hue.a = static_cast<u8>(alpha);
            Menu_DrawRectangle(cx - w / 2, foot - ph, w, ph, hue, 1);
        }
        // Nearest last, for the pointer (ShelfHit).
        spineRects.push_back({p.index, cx - w / 2, foot - ph, cx + w / 2, foot});
    }
    DrawShelfCaption(alpha);
}

bool GuiGameGrid::ShelfFlightFrom(int index, ShelfFlight& out) const {
    // The ring's pictures are not boxes: a game opens with the usual zoom.
    if (ring) return false;
    if (!shelf || index < 0 || index >= Count() || index >= static_cast<int>(poses.size()) || !poses[index].placed)
        return false;
    // The disc drive's box is a plain one, and its page shows the disc's
    // game: it zooms instead.
    if ((*items)[index].badge == "DISC") return false;
    const BoxPose& p = poses[index];
    out.id = (*items)[index].id;
    out.hue = (*items)[index].hue;
    out.x = p.x;
    out.z = p.z;
    out.turn = p.turn;
    out.lift = p.lift;
    return true;
}

ShelfFlight ShelfFocusedPose(const std::string& id, GXColor hue) {
    ShelfFlight f;
    f.id = id;
    f.hue = hue;
    f.x = 320.0f;
    f.z = kPullZ;
    f.turn = static_cast<float>(M_PI) / 2;
    return f;
}

void DrawShelfFlight(const ShelfFlight& from, float toX, float toY, float toW, float toH, float e, int alpha,
                     bool reverse) {
    // Where the box's front, facing you, lands on (toX, toY, toW x toH):
    // as tall as that on screen at its depth, its middle on the spot's.
    const float k = toH / kBoxH;
    const float zFront = kEye - kEye / k;
    ShelfFlight to = from;
    to.turn = static_cast<float>(M_PI) / 2;
    to.z = zFront - kBoxT / 2;
    to.x = 320.0f + (toX + toW / 2 - 320.0f) / k;
    to.lift = kShelfY - kBoxH / 2 - (240.0f + (toY + toH / 2 - 240.0f) / k);
    const ShelfFlight& a = reverse ? to : from;
    const ShelfFlight& b = reverse ? from : to;
    // An arc: up and back out of the shelf, then down onto the spot.
    const float arc = std::sin(e * static_cast<float>(M_PI)) * 40.0f;
    const Box box = MakeBox(a.x + (b.x - a.x) * e, a.z + (b.z - a.z) * e, a.turn + (b.turn - a.turn) * e,
                            a.lift + (b.lift - a.lift) * e + arc);
    const u8* art = riftwii::wii::BoxTexture(from.id);
    const u8* cover = art ? nullptr : riftwii::wii::CoverTexture(from.id);
    Face faces[5];
    BoxFaces(from.id, box, art, cover, from.hue, faces);
    LoadPerspective();
    // Back to front by distance from the eye: the faces of one box.
    int order[5] = {0, 1, 2, 3, 4};
    const auto depth = [&](int i) {
        const Face& f = faces[i];
        return (f.c[0].z + f.c[2].z) / 2;
    };
    std::sort(order, order + 5, [&](int x, int y) { return depth(x) < depth(y); });
    for (const int i : order)
        if (FacesEye(faces[i])) DrawFace(faces[i], alpha);
    LoadFlat();
}

void SetShelfFlying(const std::string& id) { g_flying = id; }

void GuiGameGrid::UpdateShelf(GuiTrigger* t) {
    // + and -: one box at a time (a grid's page means nothing on the
    // shelf: it skipped 12 boxes), and held, they keep going.
    if (t->chan >= 0 && t->chan < 4) {
        const int step = HeldStep(t);
        int& held = shelfStepHeld[t->chan];
        if (step == 0) {
            held = 0;
        } else {
            ++held;
            if (PressedPage(t, step) || (held > 20 && held % 5 == 0)) ShelfStep(step);
            return;
        }
    }
    // Up or Down: the focused box turns round (not again until it has).
    // Without GameTDB's full scan its back is made up (DrawMadeUpBack).
    if (!ring && (t->Up() || t->Down()) && shelfStill > 15) {
        shelfFlip = !shelfFlip;
        shelfStill = 0;
        soundOver->Play();
    }
    if (t->wpad && t->wpad->ir.valid) {
        const float x = t->wpad->ir.x;
        const int hit = ShelfHit(static_cast<int>(x), static_cast<int>(t->wpad->ir.y));
        if (!dragging) {
            if (hit != shelfHover && hit >= 0) soundOver->Play();
            shelfHover = hit;
        }
        const u32 a = WPAD_BUTTON_A;
        if (dragArmed && t->chan != dragChan) {
            // Another controller's pointer while one drags: only hovers.
        } else if (t->wpad->btns_d & a) {
            // A goes down: a click, unless the pointer moves off with it held.
            dragArmed = true;
            dragging = false;
            dragPress = hit;
            dragChan = t->chan;
            dragStartX = dragLastX = x;
            dragAccum = dragSpeed = 0.0f;
            glide = glideAccum = 0.0f;
        } else if (dragArmed && (t->wpad->btns_h & a)) {
            const float dx = x - dragLastX;
            dragLastX = x;
            if (!dragging && std::fabs(x - dragStartX) > 16.0f) {
                dragging = true;
                shelfHover = -1;
                dragAccum = x - dragStartX;
            } else if (dragging) {
                dragAccum += dx;
            }
            if (dragging) {
                dragSpeed = dragSpeed * 0.5f + dx * 0.5f;
                // Dragged right: the shelf moves right, the focus left.
                while (dragAccum >= kDragStep) {
                    ShelfStep(-1);
                    dragAccum -= kDragStep;
                }
                while (dragAccum <= -kDragStep) {
                    ShelfStep(1);
                    dragAccum += kDragStep;
                }
            }
        } else if (dragArmed) {
            dragArmed = false;
            if (dragging) {
                dragging = false;
                glide = std::clamp(dragSpeed, -24.0f, 24.0f);
                glideAccum = dragAccum;
            } else if (dragPress >= 0) {
                if (dragPress == focus) {
                    clicked = focus;
                    soundClick->Play();
                } else {
                    // A box on the shelf is pulled out first; A again opens it.
                    focus = dragPress;
                    page = focus / kPerPage;
                    shelfHover = -1;
                    soundClick->Play();
                }
            }
        } else if (PressedA(t) && hit >= 0) {
            // A Classic Controller's or a GameCube controller's A while pointing.
            if (hit == focus) {
                clicked = focus;
                soundClick->Play();
            } else {
                focus = hit;
                page = focus / kPerPage;
                shelfHover = -1;
                soundClick->Play();
            }
        }
        return;
    }
    // The dragging controller's pointer left the screen: the drag ends there.
    if (dragArmed && t->chan == dragChan) {
        dragArmed = false;
        if (dragging) {
            dragging = false;
            glide = std::clamp(dragSpeed, -24.0f, 24.0f);
            glideAccum = dragAccum;
        }
    }
    if (AnyPointer()) return;
    shelfHover = -1;
    const bool right = t->Right(), left = !right && t->Left();
    if (right || left) {
        // Steps close together (a direction held): two, then three at a time.
        const u64 now = gettime();
        shelfStreak = shelfLastStep != 0 && diff_msec(shelfLastStep, now) < 300 ? shelfStreak + 1 : 0;
        shelfLastStep = now;
        ShelfStep((right ? 1 : -1) * (shelfStreak > 12 ? 3 : shelfStreak > 5 ? 2 : 1));
    }
    if (PressedA(t)) {
        clicked = focus;
        soundClick->Play();
    }
}
