// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "madecover.hpp"

#include <gccore.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "libwiigui/gui.h"
#include "drawtrace.hpp"
#include "log.hpp"
#include "riftwii/coverart.hpp"
#include "skin.hpp"
#include "video.h"

namespace riftwii::wii {
namespace {

namespace skin = riftwii::wii::skin;

// Drawn at twice the cover's size and halved by the copy (its box
// filter), so the name's edges come out smooth at the cover's size.
constexpr int kDrawW = riftwii::kCoverWidth * 2;
constexpr int kDrawH = riftwii::kCoverHeight * 2;
constexpr std::size_t kCoverBytes = static_cast<std::size_t>(riftwii::kCoverWidth) * riftwii::kCoverHeight * 2;
// Two pages of the covers view and a shelf's worth of boxes on either
// side of the focus fit at once; about 860 KB of MEM2, taken the first
// time a game needs one.
constexpr int kSlots = 48;
// A page turn asks for twelve, all made the next frame, each a little
// drawing and one copy.
constexpr int kPerFrame = 12;
constexpr std::size_t kMaxWaiting = 32;
// As the stored covers' (src/art/coverart.cpp), so both sit alike in the
// covers view's tiles.
constexpr float kCornerRadius = 8.0f;
constexpr std::size_t kMaskBytes = static_cast<std::size_t>(riftwii::kCoverWidth) * riftwii::kCoverHeight;

// Where the name goes on the drawn (2x) cover: under the blank cover's
// band across the top, above its strip along the foot.
constexpr int kTitleSize = 24;
// A word too wide for the cover at kTitleSize steps the name down to this
// size at the least, rather than being cut between its letters.
constexpr int kTitleMinSize = 18;
constexpr int kTitleLineGap = 27;
constexpr int kTitleLines = 4;
constexpr int kTitleMargin = 14;
constexpr int kTitleTop = 50;
constexpr int kTitleBottom = 190;
constexpr int kIdSize = 20;  // 10 on screen: at 8 a composite cable blurred it away
constexpr int kIdGap = 30;  // the ID's line under the name, a little apart

enum class SlotState { Empty, Ready };

struct Slot {
    std::string key;
    std::string label;  // the title and ID printed on it
    u8* data = nullptr;
    SlotState state = SlotState::Empty;
    u32 used = 0;  // the frame it was last asked for
};

struct Request {
    std::string key, label, title, id;
};

Slot g_slots[kSlots];
bool g_poolTried = false;
bool g_poolReady = false;
u32 g_frame = 1;
std::vector<Request> g_waiting;
GuiText* g_text = nullptr;
u8* g_mask = nullptr;  // I8: how much of each texel the rounded corners keep
bool g_inkChosen = false;
GXColor g_ink = {46, 49, 56, 255};
// The ID's ink, darker than a dim grey: a composite cable left that at
// about 1.6:1 on the white cover.
GXColor g_inkDim = {84, 88, 98, 255};
bool g_spineInkChosen = false;
GXColor g_spineInk = {46, 49, 56, 255};
// Light ink on a blank cover darker than this (Rec. 601 brightness).
constexpr long kLightPaper = 140;

bool PoolReady() {
    if (g_poolTried) return g_poolReady;
    g_poolTried = true;
    u8* block = skin::Mem2Alloc(kCoverBytes * kSlots + kMaskBytes);
    if (!block) {
        logf("Covers: no memory to make up covers for games without one; their tiles show the name instead\n");
        return false;
    }
    for (int i = 0; i < kSlots; ++i) g_slots[i].data = block + kCoverBytes * i;
    // The corners' mask, in I8's 8x4 texel tiles. The frame buffer has no
    // alpha, so a copy is square; the mask rounds it as it is drawn (a
    // copy can't be rounded in memory: Dolphin keeps copies on the GPU).
    g_mask = block + kCoverBytes * kSlots;
    const int w = riftwii::kCoverWidth, h = riftwii::kCoverHeight;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float dx = px < kCornerRadius ? kCornerRadius - px : px > w - kCornerRadius ? px - (w - kCornerRadius) : 0.0f;
            const float dy = py < kCornerRadius ? kCornerRadius - py : py > h - kCornerRadius ? py - (h - kCornerRadius) : 0.0f;
            float keep = 1.0f;
            if (dx > 0.0f && dy > 0.0f) keep = std::clamp(kCornerRadius - std::sqrt(dx * dx + dy * dy) + 0.5f, 0.0f, 1.0f);
            const std::size_t at = (static_cast<std::size_t>(y / 4) * (w / 8) + x / 8) * 32 + (y % 4) * 8 + x % 8;
            g_mask[at] = static_cast<u8>(keep * 255.0f + 0.5f);
        }
    }
    DCFlushRange(g_mask, kMaskBytes);
    g_poolReady = true;
    return true;
}

// Whether the blank cover is light from x0 to x1 and y0 to y1 (its
// average brightness, every `step` pixels), read from the picture's RGBA8
// tiles (32 bytes of alpha and red, then 32 of green and blue, for each
// 4x4 tile). Light when there is no picture to read: the painted one is.
bool BlankIsLight(int x0, int x1, int y0, int y1, int step) {
    const skin::Tex& t = skin::blankCover;
    if (!t.data || t.w != kDrawW || t.h != kDrawH) return true;
    long sum = 0, count = 0;
    for (int y = y0; y < y1; y += step) {
        for (int x = x0; x < x1; x += step) {
            const std::size_t tile = static_cast<std::size_t>(y / 4) * (t.w / 4) + x / 4;
            const int texel = (y % 4) * 4 + x % 4;
            const u8* ar = t.data + tile * 64;
            const u8* gb = ar + 32;
            sum += 299 * ar[texel * 2 + 1] + 587 * gb[texel * 2] + 114 * gb[texel * 2 + 1];
            ++count;
        }
    }
    return count == 0 || sum / count >= kLightPaper * 1000;
}

// Dark ink on a light blank cover, light ink on a dark one (a theme's
// own picture), judged where the name goes.
void ChooseInk() {
    g_inkChosen = true;
    if (!BlankIsLight(kTitleMargin, kDrawW - kTitleMargin, kTitleTop, kTitleBottom, 6)) {
        g_ink = {244, 244, 246, 255};
        g_inkDim = {214, 216, 224, 255};
    }
}

// Words onto lines while they fit, centred later; a name without spaces
// (Japanese) is cut by characters; the last line ends in "..." when the
// name goes on.
std::vector<std::string> WrapName(const std::string& text, int widthLimit, int lines) {
    const auto width_of = [](const std::string& s) {
        g_text->SetText(s.c_str());
        return g_text->GetTextWidth();
    };
    const auto boundary = [](const std::string& s, std::size_t n) {
        while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
        return n;
    };
    std::vector<std::string> out;
    std::string rest = text;
    for (int l = 0; l < lines; ++l) {
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
        if (rest.empty()) break;
        std::string line;
        std::size_t at = 0;
        while (at < rest.size()) {
            std::size_t next = rest.find_first_of(" -", at);
            if (next == std::string::npos) next = rest.size();
            else if (rest[next] == '-') ++next;  // the hyphen stays on the line
            const std::string candidate = rest.substr(0, next);
            if (width_of(candidate) > widthLimit) break;
            line = candidate;
            at = next < rest.size() && rest[next] == ' ' ? next + 1 : next;
        }
        if (line.empty()) {
            std::size_t n = rest.size();
            while (n > 1 && width_of(rest.substr(0, n)) > widthLimit) n = boundary(rest, n - 1);
            line = rest.substr(0, n);
            at = n;
        }
        rest = at < rest.size() ? rest.substr(at) : "";
        const bool last = l == lines - 1;
        if (last && !rest.empty()) {
            while (line.size() > 1 && width_of(line + "...") > widthLimit) line.resize(boundary(line, line.size() - 1));
            while (!line.empty() && line.back() == ' ') line.pop_back();
            line += "...";
        }
        out.push_back(line);
    }
    return out;
}

// The size the name is drawn at: kTitleSize, or smaller until each of its
// words fits on a line ("WarioWare:" had been cut into "WarioWare" and a
// line starting ": DIY"). A word is cut where WrapName may break a line:
// at a space, or after a hyphen, which stays with the part before it.
// Words in a script written without spaces (Japanese: three-byte UTF-8)
// are cut by characters as before.
int TitleSizeFor(const std::string& title, int widthLimit) {
    std::vector<std::string> words;
    std::size_t at = 0;
    while (at < title.size()) {
        std::size_t end = title.find_first_of(" -", at);
        if (end == std::string::npos) end = title.size();
        else if (title[end] == '-') ++end;
        if (end > at) words.push_back(title.substr(at, end - at));
        at = end < title.size() && title[end] == ' ' ? end + 1 : end;
    }
    // Kana, CJK ideographs, Hangul and full-width forms: the scripts written
    // without spaces. Not every three-byte character (curly quotes, the
    // trade mark sign), which a Latin word may hold.
    const auto spaceless_script = [](const std::string& word) {
        for (std::size_t i = 0; i + 2 < word.size(); ++i) {
            const unsigned char lead = static_cast<unsigned char>(word[i]);
            if ((lead & 0xF0) != 0xE0) continue;
            const unsigned code = ((lead & 0x0Fu) << 12) |
                                  ((static_cast<unsigned char>(word[i + 1]) & 0x3Fu) << 6) |
                                  (static_cast<unsigned char>(word[i + 2]) & 0x3Fu);
            const bool kana = code >= 0x3040 && code <= 0x30FF;
            const bool ideograph = (code >= 0x3400 && code <= 0x9FFF) || (code >= 0xF900 && code <= 0xFAFF);
            const bool hangul = code >= 0xAC00 && code <= 0xD7AF;
            const bool full_width = code >= 0xFF00 && code <= 0xFFEF;
            if (kana || ideograph || hangul || full_width) return true;
        }
        return false;
    };
    int size = kTitleSize;
    for (; size > kTitleMinSize; --size) {
        g_text->SetFontSize(size);
        bool allFit = true;
        for (const std::string& word : words) {
            if (spaceless_script(word)) continue;
            g_text->SetText(word.c_str());
            if (g_text->GetTextWidth() > widthLimit) {
                allFit = false;
                break;
            }
        }
        if (allFit) break;
    }
    return size;
}

void DrawCentred(const std::string& text, int size, GXColor colour, int top) {
    g_text->SetFontSize(size);
    g_text->SetColor(colour);
    g_text->SetText(text.c_str());
    g_text->SetPosition((kDrawW - g_text->GetTextWidth()) / 2, top);
    g_text->Draw();
}

// The blank cover and its name into the frame buffer's top left corner,
// then copied out at half the size into `slot`. The windows draw over
// that corner afterwards (the backdrop fills the whole screen first).
void Make(const Request& request, Slot& slot) {
    skin::Draw(skin::blankCover, 0, 0, 255, 1.0f);
    const int titleWidth = kDrawW - 2 * kTitleMargin;
    const int titleSize = TitleSizeFor(request.title, titleWidth);
    const int lineGap = kTitleLineGap * titleSize / kTitleSize;
    g_text->SetFontSize(titleSize);
    const std::vector<std::string> lines = WrapName(request.title, titleWidth, kTitleLines);
    // The name and the ID under it, together in the middle of the space
    // between the band and the foot (the covers view's badges sit there).
    const int idHeight = request.id.empty() ? 0 : kIdGap;
    const int blockHeight = static_cast<int>(lines.size()) * lineGap + idHeight;
    int top = (kTitleTop + kTitleBottom - blockHeight) / 2;
    for (const std::string& line : lines) {
        DrawCentred(line, titleSize, g_ink, top);
        top += lineGap;
    }
    if (!request.id.empty()) DrawCentred(request.id, kIdSize, g_inkDim, top + kIdGap - lineGap);

    GX_SetTexCopySrc(0, 0, kDrawW, kDrawH);
    GX_SetTexCopyDst(riftwii::kCoverWidth, riftwii::kCoverHeight, GX_TF_RGB5A3, GX_TRUE);
    Menu_SetPictureCopy(true);
    GX_CopyTex(slot.data, GX_TRUE);
    GX_PixModeSync();
    Menu_SetPictureCopy(false);
    slot.key = request.key;
    slot.label = request.label;
    slot.state = SlotState::Ready;
    slot.used = g_frame;
}

// An empty slot, else the one shown longest ago, but none shown this
// frame or the last (it is on screen).
Slot* FreeSlot() {
    Slot* pick = nullptr;
    for (Slot& s : g_slots) {
        if (s.state == SlotState::Empty) return &s;
        if (s.used + 1 >= g_frame) continue;
        if (!pick || s.used < pick->used) pick = &s;
    }
    return pick;
}

}  // namespace

const u8* MadeUpCover(const std::string& key, const std::string& title, const std::string& id) {
    if (key.empty() || !skin::blankCover.data || (g_poolTried && !g_poolReady)) return nullptr;
    const std::string label = title + '\n' + id;
    for (Slot& s : g_slots) {
        if (s.state == SlotState::Empty || s.key != key) continue;
        if (s.label != label) {
            // The name changed (GameTDB's names came): made again.
            s.key.clear();
            break;
        }
        s.used = g_frame;
        return s.data;
    }
    for (const Request& r : g_waiting)
        if (r.key == key) return nullptr;
    if (g_waiting.size() < kMaxWaiting) g_waiting.push_back({key, label, title, id});
    return nullptr;
}

const u8* MadeUpCoverMade(const std::string& key) {
    // A slot whose name went stale has an empty key until it is reused:
    // never one to hand the disc drive's box (no ID).
    if (key.empty()) return nullptr;
    for (Slot& s : g_slots) {
        if (s.state != SlotState::Ready || s.key != key) continue;
        s.used = g_frame;
        return s.data;
    }
    return nullptr;
}

bool IsMadeUpCover(const u8* cover) {
    if (!cover) return false;
    for (const Slot& s : g_slots) {
        if (s.data == cover) return true;
    }
    return false;
}

void MakeUpCovers() {
    ++g_frame;
    if (g_waiting.empty() || !PoolReady()) {
        g_waiting.clear();
        return;
    }
    if (!g_text) {
        g_text = new GuiText(nullptr, kTitleSize, g_ink);
        g_text->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    }
    if (!g_inkChosen) ChooseInk();

    // In frame buffer pixels, with no camera (a transition's zoom) and no
    // display scale (a 16:9 menu's narrowing): the copy takes pixels.
    Menu_PushNoCamera();
    Mtx44 projection;
    guOrtho(projection, 0, static_cast<f32>(Menu_EfbHeight()), 0, static_cast<f32>(Menu_XfbWidth()), 0, 300);
    GX_LoadProjectionMtx(projection, GX_ORTHOGRAPHIC);
    GX_SetScissor(0, 0, kDrawW, kDrawH);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    std::size_t made = 0;
    while (made < g_waiting.size() && made < static_cast<std::size_t>(kPerFrame)) {
        Slot* slot = FreeSlot();
        if (!slot) break;
        Make(g_waiting[made], *slot);
        ++made;
    }
    // The textures drawn this frame may be the slots just written.
    GX_InvalidateTexAll();
    g_waiting.erase(g_waiting.begin(), g_waiting.begin() + static_cast<std::ptrdiff_t>(made));
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
    GX_SetScissor(0, 0, static_cast<u32>(Menu_XfbWidth()), static_cast<u32>(Menu_EfbHeight()));
    Menu_PopCamera();
}


void DrawMadeUpCover(const u8* cover, float x, float y, int alpha, float scale) {
    if (!cover || !g_mask || alpha <= 0) return;
    const int w = riftwii::kCoverWidth, h = riftwii::kCoverHeight;
    GxTraceTexture(cover, w, h, GX_TF_RGB5A3);
    GXTexObj tex, mask;
    GX_InitTexObj(&tex, const_cast<u8*>(cover), w, h, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObj(&mask, g_mask, w, h, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_LoadTexObj(&tex, GX_TEXMAP0);
    GX_LoadTexObj(&mask, GX_TEXMAP1);
    // Stage 0: the cover times the vertex colour (the fade), as
    // skin::DrawRgb5a3 draws; stage 1: its alpha times the mask's.
    GX_SetNumTevStages(2);
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLORNULL);
    GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_APREV, GX_CA_TEXA, GX_CA_ZERO);
    GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    // Scaled about the centre in libgui's 2D view, as skin::DrawRgb5a3.
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
    GX_SetNumTevStages(1);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GX_SetVtxDesc(GX_VA_TEX0, GX_NONE);
}

GXColor MadeUpSpineInk() {
    if (!g_spineInkChosen) {
        g_spineInkChosen = true;
        // Where the name runs down the spine: under the band, above the
        // foot strip, as on the cover.
        if (!BlankIsLight(kMadeUpSpineLeft, kMadeUpSpineRight, kTitleTop, kTitleBottom, 2))
            g_spineInk = {244, 244, 246, 255};
    }
    return g_spineInk;
}

}  // namespace riftwii::wii
