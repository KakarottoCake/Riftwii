// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Home grid's shelf: the games' boxes standing spine out on a plank,
// in perspective. The focused box slides out towards you and turns to show
// its front; the boxes beside it lean apart to make room. Each box's
// corners are worked out here, once a frame: the same points are drawn,
// tested for which faces look at you, and used to find the box a pointer
// is on.
#include <gccore.h>

#include <algorithm>
#include <cmath>

#include "boxart.hpp"
#include "covers.hpp"
#include "gui_gamegrid.hpp"
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
constexpr float kBoxW = 125.0f;  // spine to front edge, along z
constexpr float kBoxT = 18.0f;   // the spine's width
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
};

void DrawFace(const Face& f, int alpha) {
    // Lit from above and the left, never darker than half.
    const float light = 0.62f + 0.38f * std::max(0.0f, (f.normal.z * 0.85f - f.normal.y * 0.35f - f.normal.x * 0.25f));
    const auto shade = [&](u8 c) { return static_cast<u8>(std::min(255.0f, c * light)); };
    const u8 r = shade(f.color.r), g = shade(f.color.g), b = shade(f.color.b);
    const u8 a = static_cast<u8>(f.color.a * alpha / 255);
    if (f.tex) {
        GXTexObj tex;
        GX_InitTexObj(&tex, const_cast<u8*>(f.tex), static_cast<u16>(f.texW), static_cast<u16>(f.texH),
                      f.rgba8 ? GX_TF_RGBA8 : GX_TF_RGB5A3, f.repeat ? GX_REPEAT : GX_CLAMP, GX_CLAMP, GX_FALSE);
        GX_InitTexObjLOD(&tex, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
        GX_LoadTexObj(&tex, GX_TEXMAP0);
        GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    } else {
        GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
    }
    const float us[4] = {f.u0, f.u1, f.u1, f.u0};
    const float vs[4] = {f.v0, f.v0, f.v1, f.v1};
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; ++i) {
        GX_Position3f32(f.c[i].x, f.c[i].y, f.c[i].z);
        GX_Color4u8(r, g, b, a);
        if (f.tex) GX_TexCoord2f32(us[i], vs[i]);
    }
    GX_End();
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

// A box's five faces you can see (not its bottom), covered with its scan,
// its cover or plain plastic in the game's colour. Spine, front, back,
// top, open edge.
void BoxFaces(const Box& b, const u8* art, const u8* cover, GXColor hue, Face (&faces)[5]) {
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
    }
    // No scan: the back in the game's colour, as a case's back is never white.
    if (!art)
        faces[2].color = {static_cast<u8>(hue.r / 2 + 70), static_cast<u8>(hue.g / 2 + 70),
                          static_cast<u8>(hue.b / 2 + 70), 255};
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

}  // namespace

std::vector<int> GuiGameGrid::ShelfWanted() const {
    std::vector<int> out;
    if (!shelf) return out;
    for (int d = 0; d <= kReach; ++d) {
        if (focus + d < Count()) out.push_back(focus + d);
        if (d > 0 && focus - d >= 0) out.push_back(focus - d);
    }
    return out;
}

void GuiGameGrid::BoxArrived(const std::string& id) { riftwii::wii::ForgetBox(id); }

void GuiGameGrid::ShelfStep(int delta) {
    const int target = std::clamp(focus + delta, 0, Count() - 1);
    if (target == focus) return;
    focus = target;
    page = focus / kPerPage;
    soundOver->Play();
}

int GuiGameGrid::ShelfHit(int x, int y) const {
    // The nearest drawn box first: they were stored back to front.
    for (auto it = spineRects.rbegin(); it != spineRects.rend(); ++it)
        if (x >= it->x0 && x < it->x1 && y >= it->y0 && y < it->y1) return it->index;
    return -1;
}

void GuiGameGrid::DrawShelf(int alpha) {
    if (static_cast<int>(poses.size()) != Count()) poses.assign(Count(), BoxPose{});
    // Enough boxes each side to reach past the screen's edges (a widescreen
    // menu's are further out), so they come and go off screen: at kReach
    // they appeared and vanished in view on a 16:9 menu.
    f32 vx, vy, vw, vh;
    Menu_VisibleArea(&vx, &vy, &vw, &vh);
    const int reach = std::max(kReach, static_cast<int>((320.0f - vx + 60.0f - kOpen - kBoxT / 2) / kPitch) + 2);
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
        BoxPose want;
        if (d == 0) {
            want.x = 320.0f;
            want.z = kPullZ;
            want.turn = static_cast<float>(M_PI) / 2;
        } else {
            const float off = kOpen + kBoxT / 2 + (ad - 1) * kPitch;
            want.x = 320.0f + (d < 0 ? -off : off);
        }
        want.lift = i == shelfHover && d != 0 ? kLift : 0.0f;
        BoxPose& p = poses[i];
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

    LoadPerspective();
    // The plank: its top, then its front edge, wider than the screen.
    {
        const bool pic = skin::shelfPlank.data != nullptr;
        const GXColor wood = pic ? GXColor{255, 255, 255, 255} : skin::kShelfWood;
        const GXColor edgeWood = pic ? GXColor{255, 255, 255, 255} : skin::kShelfEdge;
        const auto textured = [&](Face& f, float v0, float v1) {
            if (!pic) return;
            f.tex = skin::shelfPlank.data;
            f.texW = skin::shelfPlank.w;
            f.texH = skin::shelfPlank.h;
            f.rgba8 = true;
            f.repeat = true;  // 256x64 (src/theme.cpp)
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
        BoxFaces(b, art, cover, item.hue, faces);
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        bool spineShown = false;
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
            band.color = item.hue;
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
        if (item.badge == "DISC" && !art && !cover && FacesEye(faces[1]) && skin::iconDisc.data) {
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

    // The focused (or pointed at) game's name under the shelf.
    const int shown = shelfHover >= 0 ? shelfHover : focus;
    if (shown != captionFor) {
        captionFor = shown;
        measure->SetFontSize(18);
        caption->SetText(shown >= 0 && shown < Count() ? Fit((*items)[shown].title, 560).c_str() : "");
        measure->SetFontSize(13);
    }
    caption->Draw();
}

bool GuiGameGrid::ShelfFlightFrom(int index, ShelfFlight& out) const {
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
    BoxFaces(box, art, cover, from.hue, faces);
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
    if (PressedPage(t, -1) || PressedPage(t, 1)) {
        ShelfStep(PressedPage(t, -1) ? -kPerPage : kPerPage);
        return;
    }
    if (t->wpad && t->wpad->ir.valid) {
        const int hit = ShelfHit(static_cast<int>(t->wpad->ir.x), static_cast<int>(t->wpad->ir.y));
        if (hit != shelfHover && hit >= 0) soundOver->Play();
        shelfHover = hit;
        if (PressedA(t) && hit >= 0) {
            if (hit == focus) {
                clicked = focus;
                soundClick->Play();
            } else {
                // A box on the shelf is pulled out first; A again opens it.
                focus = hit;
                page = focus / kPerPage;
                shelfHover = -1;
                soundClick->Play();
            }
        }
        return;
    }
    if (AnyPointer()) return;
    shelfHover = -1;
    if (t->Right()) ShelfStep(1);
    else if (t->Left()) ShelfStep(-1);
    if (PressedA(t)) {
        clicked = focus;
        soundClick->Play();
    }
}
