// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_gamegrid.hpp"
#include "loadersettings.hpp"
#include "transition.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "covers.hpp"
#include "menutheme.hpp"
#include "riftwii/coverart.hpp"
#include "riftwii/titles.hpp"
#include "riftwii/skinpaint.hpp"
#include "skin.hpp"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kTitleSize = 15;       // names: the tile's two lines
constexpr int kCoverTitleSize = 13;  // covers: a tile without a cover
constexpr int kCaptionSize = 18;     // covers: the lit game's name
constexpr int kCaptionY = 266;
constexpr int kShelfCaptionY = 276;  // under the shelf (wii/menu/gui_shelf.cpp)
constexpr int kChannelLabelSize = 15;  // channels: the lit icon's name
constexpr int kTextLeft = 11;
constexpr int kCoverTextLeft = 6;
// Everything on Home stays inside a TV's overscan: 5% of the screen in from
// each edge (x 32 to 608, y 24 to 456 on a 4:3 menu), which the Wii U and
// many TVs crop. The page arrows (36 across in a 48 canvas, drawn 2 to the
// left) end at those edges.
constexpr int kArrowLeftX = 26, kArrowRightX = 570;
// The tiles between the arrows: covers at 92% of the stored size (6
// across), 20% larger than stored on a widescreen menu, which has the room; names and channels 92%
// as wide, as wide again as a widescreen menu is.
constexpr float kCoverScale4x3 = 0.925f, kCoverScaleWide = 1.2f;
constexpr float kTileNarrow = 0.92f;

// How much wider than 640 the menu is (1 on a 4:3 menu).
float WideFactor() {
    if (!skin::WideMenu()) return 1.0f;
    f32 safeX, safeW;
    Menu_SafeArea(&safeX, &safeW);
    return safeW > 640.0f ? safeW / 640.0f : 1.0f;
}
float CoverScale() { return WideFactor() > 1.0f ? kCoverScaleWide : kCoverScale4x3; }

// The frame's pixels per menu unit across at the full screen size (a
// widescreen menu fits 4/3 as many units into the same pixels).
float PixelsPerUnitX() { return skin::WideMenu() ? 0.75f : 1.0f; }

// `u` (menu units across, or down) moved onto the nearest edge between
// two of the frame's pixels, as the display scale puts it.
float SnapX(float u) {
    const float k = 320.0f / (320.0f - Menu_ScreenToMenuX(0.0f));  // pixels per unit
    return 320.0f + std::round((u - 320.0f) * k) / k;
}
float SnapY(float u) {
    const float k = 240.0f / (240.0f - Menu_ScreenToMenuY(0.0f)) * Menu_EfbHeight() / 480.0f;
    return 240.0f + std::round((u - 240.0f) * k) / k;
}

// A channel's icon is 128 x 96 and drawn one texel to a pixel, as the Wii
// Menu does: shrunk into a smaller tile (it was 84%) every pixel of the
// game's art was resampled and the icons looked fuzzy. The channel shows
// it 128 x 88, a few lines cut at its top and bottom (the Wii Menu's
// shape); 12 apart, so the page arrows sit over the outer columns' edges.
constexpr float kIconPixelsW = 128.0f, kIconPixelsH = 96.0f;
constexpr int kChannelPixelsH = 88, kChannelGapPixelsX = 12, kChannelGapY = 10;
// As the other views' tiles; the search button moves to the bar
// (rift_menu.cpp), the fourth column reaching the corner it had.
constexpr int kChannelTop = 30;
// Roomy spacing (grid_spacing = roomy): a tester found the grid cramped
// beside the Wii Menu, which leaves wider gaps at the sides. The channels
// are 7/8 the size (112 x 77, the icons scaled with them, so no longer
// one texel to a pixel) and 16 apart, which leaves about 70 pixels at each
// side on a 4:3 menu and keeps the page arrows (x 26 to 62 and 570 to 606)
// clear of the tiles, even lit; the grid stays centred down the screen.
constexpr float kRoomyIconScale = 0.875f;
constexpr int kRoomyChannelGapPixelsX = 16, kRoomyChannelGapY = 14, kRoomyChannelTop = 42;

// Names: 4x3 wide tiles. Covers: 6x2 at the stored cover size, the name
// line under them. Both between the arrows.
constexpr int kNameCols = 4;
constexpr int kCoverCols = 6;

// A turned page slides in from this far to the side, easing in.
constexpr float kSlideFrom = 150.0f;
// Channels with page_peek on: the neighbouring pages' tiles at this much
// of their alpha, more while the pointer is on them (that page's arrow).
constexpr float kPeekDim = 0.5f, kPeekLit = 0.8f;

bool PressedPage(GuiTrigger* t, int delta) {
    const u32 wpad = delta < 0 ? (WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS) : (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS);
    const u32 drc = delta < 0 ? WIIDRC_BUTTON_MINUS : WIIDRC_BUTTON_PLUS;
    return (t->wpad && (t->wpad->btns_d & wpad)) || (t->wiidrcdata.btns_d & drc);
}

bool AnyPointer() {
    for (int i = 0; i < 4; i++)
        if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
    return false;
}

bool PressedA(GuiTrigger* t) {
    return (t->wpad && (t->wpad->btns_d & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))) || (t->pad.btns_d & PAD_BUTTON_A) ||
           (t->wiidrcdata.btns_d & WIIDRC_BUTTON_A);
}

bool HeldA(GuiTrigger* t) {
    return (t->wpad && (t->wpad->btns_h & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))) || (t->pad.btns_h & PAD_BUTTON_A) ||
           (t->wiidrcdata.btns_h & WIIDRC_BUTTON_A);
}

// B as the grid's letters take it: L on a GameCube controller, whose B
// has no place beside A for holding both.
bool HeldLetterB(GuiTrigger* t) {
    return (t->wpad && (t->wpad->btns_h & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B))) || (t->pad.btns_h & PAD_TRIGGER_L) ||
           (t->wiidrcdata.btns_h & WIIDRC_BUTTON_B);
}

bool PressedB(GuiTrigger* t) {
    return (t->wpad && (t->wpad->btns_d & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B))) || (t->pad.btns_d & PAD_BUTTON_B) ||
           (t->wiidrcdata.btns_d & WIIDRC_BUTTON_B);
}

// A game picked up is drawn this much larger, over the others.
constexpr float kCarryScale = 1.12f;
// The pointer waits this many frames at a side before the page turns
// (half a second), and this many more for each page after.
constexpr int kEdgeTurnFrames = 30, kEdgeRepeatFrames = 45;

GuiText* MakeText(GuiElement* parent, int size, GXColor color) {
    GuiText* t = new GuiText(nullptr, size, color);
    t->SetParent(parent);
    t->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    return t;
}

// A cut at byte `n` moved back to the start of a UTF-8 character, so a
// name is never split inside one.
std::size_t CharBoundary(const std::string& s, std::size_t n) {
    while (n > 0 && n < s.size() && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
    return n;
}

}  // namespace

GuiGameGrid::GuiGameGrid() {
    width = screenwidth;
    height = screenheight;
    selectable = true;
    for (Slot* set : {slots, leaving}) {
        for (int i = 0; i < kPerPage; ++i) {
            Slot& s = set[i];
            for (GuiText*& line : s.lines) line = MakeText(this, kTitleSize, skin::kInk);
            s.id = MakeText(this, 11, skin::kInkDim);
            s.badge = MakeText(this, 10, skin::kInkSoft);
            s.mods = MakeText(this, 10, skin::kTextOnAccent);
        }
    }
    measure = new GuiText(nullptr, kTitleSize, skin::kInk);
    caption = new GuiText(nullptr, kCaptionSize, skin::kInk);
    caption->SetParent(this);
    caption->SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
    caption->SetPosition(0, kCaptionY);
    captionInfo = new GuiText(nullptr, 14, skin::kInkSoft);
    captionInfo->SetParent(this);
    captionInfo->SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
    captionInfo->SetPosition(0, kShelfCaptionY + 24);
    spineText = new GuiText(nullptr, 13, GXColor{40, 40, 48, 255});
    spineText->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
    for (int& item : shownItem) item = -1;
}

GuiGameGrid::~GuiGameGrid() {
    for (Slot* set : {slots, leaving}) {
        for (int i = 0; i < kPerPage; ++i) {
            for (GuiText* line : set[i].lines) delete line;
            delete set[i].id;
            delete set[i].badge;
            delete set[i].mods;
        }
    }
    delete measure;
    delete caption;
    delete captionInfo;
    delete spineText;
    delete soundOver;
    delete soundClick;
}

// Channels on a widescreen menu: the Wii Menu stretches its 4:3 menu
// across a 16:9 screen, its channels with it; so does the grid (its
// tiles, their pictures and icons, the page arrows), about the middle;
// the icons' logos keep their shape.
float GuiGameGrid::Stretch() const {
    if (covers || shelf) return 1.0f;
    // Names and channels alike: across a widescreen menu's width.
    return kTileNarrow * WideFactor();
}

const GuiGameGrid::Geometry& GuiGameGrid::Geo() const {
    // The top row starts 6 below the overscan's edge (y 24): room for the lit ring.
    // 120 x 84, the Wii Menu's channel shape (about 1.43 to 1 on a 4:3
    // menu); clear of the search button (x 568).
    static const Geometry names = {kNameCols, 120, 84, 61, 30, 13, 12};  // channels: channelGeo
    // Roomy: 9/10 the size and 18 apart, about 77 at each side, the rows
    // centred where the normal ones are.
    static const Geometry roomyNames = {kNameCols, 108, 76, 77, 40, 18, 14};
    if (covers) {
        // Six across about the middle, 8 apart (12 on a widescreen menu).
        const float k = CoverScale();
        const int w = static_cast<int>(riftwii::kCoverWidth * k + 0.5f);
        const int h = static_cast<int>(riftwii::kCoverHeight * k + 0.5f);
        const int gap = k > 1.0f ? 12 : 8;
        coverGeo = {kCoverCols, w, h, 320 - (kCoverCols * w + (kCoverCols - 1) * gap) / 2, 30, gap, 10};
        return coverGeo;
    }
    if (channels) {
        const float px = PixelsPerUnitX();
        const float iconScale = IconScale();
        const int w = static_cast<int>(kIconPixelsW * iconScale / px + 0.5f);
        const int h = static_cast<int>(kChannelPixelsH * iconScale + 0.5f);
        const int gapPixels = roomy ? kRoomyChannelGapPixelsX : kChannelGapPixelsX;
        const int gap = static_cast<int>(gapPixels / px + 0.5f);
        const int top = roomy ? kRoomyChannelTop : kChannelTop;
        const int gapY = roomy ? kRoomyChannelGapY : kChannelGapY;
        channelGeo = {kNameCols, w, h, 320 - (kNameCols * w + (kNameCols - 1) * gap) / 2, top, gap, gapY};
        return channelGeo;
    }
    const Geometry& base = roomy ? roomyNames : names;
    const float k = Stretch();
    if (k == 1.0f) return base;
    stretched = base;
    stretched.left = static_cast<int>(320 + (base.left - 320) * k + 0.5f);
    stretched.tileW = static_cast<int>(base.tileW * k + 0.5f);
    stretched.gapX = static_cast<int>(base.gapX * k + 0.5f);
    return stretched;
}

int GuiGameGrid::CoverCaptionY() const { return std::max(kCaptionY, TileY(kPerPage - 1) + Geo().tileH + 8); }

int GuiGameGrid::ArrowX(int dir) const {
    // At the safe area's edges, which a widescreen Channels view widens.
    const int base = dir < 0 ? kArrowLeftX : kArrowRightX;
    const float out = covers || shelf ? WideFactor() : Stretch() / kTileNarrow;
    return static_cast<int>(320 + (base + 22 - 320) * out + 0.5f) - 22;
}

// A tile's picture (`t` has the tile's 7-unit margin), stretched with
// the grid and scaled about the tile's middle; the tile at (x, y).
void GuiGameGrid::DrawTilePicture(const skin::Tex& t, float x, float y, int alpha, float scale) const {
    const float k = Stretch();
    if (k == 1.0f) {
        skin::Draw(t, x - 7, y - 7, alpha, scale);
        return;
    }
    if (!t.data || alpha <= 0) return;
    const float cx = x + Geo().tileW / 2.0f, cy = y + Geo().tileH / 2.0f;
    // Menu_DrawImg scales about the picture's middle: put it on the tile's.
    Menu_DrawImg(cx - t.w / 2.0f, cy - t.h / 2.0f, static_cast<u16>(t.w), static_cast<u16>(t.h), t.data, 0, scale * k,
                 scale, static_cast<u8>(alpha > 255 ? 255 : alpha));
}

int GuiGameGrid::BaseX(int slot) const { return Geo().left + (slot % Geo().cols) * (Geo().tileW + Geo().gapX); }
int GuiGameGrid::BaseY(int slot) const { return Geo().top + (slot / Geo().cols) * (Geo().tileH + Geo().gapY); }
int GuiGameGrid::TileX(int slot) const {
    return BaseX(slot) + static_cast<int>(slide) + static_cast<int>(std::lround(shiftX[slot]));
}
int GuiGameGrid::TileY(int slot) const { return BaseY(slot) + static_cast<int>(std::lround(shiftY[slot])); }

int GuiGameGrid::ArrowY() const {
    const int rows = kPerPage / Geo().cols;
    return Geo().top + (rows * Geo().tileH + (rows - 1) * Geo().gapY) / 2 - 22;
}

void GuiGameGrid::SetItems(const std::vector<GridItem>* list) {
    items = list;
    // A click not yet taken was an index into the old list: after a rescan
    // the same place is another game (the old grid stays live meanwhile).
    clicked = -1;
    // A game being moved was an index into the old list as well: it goes
    // back (a rescan, another view).
    carryFrom = carryTo = -1;
    carryDropped = false;
    shiftPage = -1;
    if (focus >= Count()) focus = Count() > 0 ? Count() - 1 : 0;
    page = focus / kPerPage;
    laidOut = false;
    leavingPage = -1;  // its names were the old list's
    captionFor = -1;
}

void GuiGameGrid::SetCovers(bool on) {
    if (covers == on) return;
    covers = on;
    for (Slot* set : {slots, leaving}) {
        for (int i = 0; i < kPerPage; ++i)
            for (GuiText* line : set[i].lines) line->SetFontSize(covers ? kCoverTitleSize : kTitleSize);
    }
    leavingPage = -1;
    laidOut = false;
    captionFor = -1;
}

void GuiGameGrid::SetRoomy(bool on) {
    if (roomy == on) return;
    roomy = on;
    laidOut = false;  // the names wrap to the tiles' new width
    captionFor = -1;
}

float GuiGameGrid::IconScale() const { return roomy ? kRoomyIconScale : 1.0f; }

void GuiGameGrid::CoverArrived(const std::string& id) { riftwii::wii::ForgetCover(id); }

void GuiGameGrid::SetChannels(bool on, IconDrawer icon) {
    channels = on;
    iconDrawer = on ? std::move(icon) : IconDrawer();
    captionFor = -1;
    if (channels) SetCovers(false);
}

void GuiGameGrid::SetCarousel(bool on) {
    if (carousel == on) return;
    carousel = on;
    poses.clear();
}

void GuiGameGrid::SetRing(bool on) {
    if (ring == on) return;
    ring = on;
    ringPlaced = false;
    poses.clear();
}

void GuiGameGrid::SetShelf(bool on) {
    if (shelf == on) return;
    shelf = on;
    poses.clear();
    spineFit.clear();
    captionFor = -1;
    infoFor = -1;
    caption->SetPosition(0, shelf ? kShelfCaptionY : kCaptionY);
    if (shelf) SetCovers(true);
}

void GuiGameGrid::Focus(int index) {
    if (index < 0 || index >= Count()) index = 0;
    focus = index;
    if (page != focus / kPerPage) {
        // Another page at once (a letter, a search): crossfaded.
        riftwii::wii::transition::BeginAuto();
        page = focus / kPerPage;
        laidOut = false;
        leavingPage = -1;
        slide = 0.0f;
    }
}

bool GuiGameGrid::TileRect(int index, float& x, float& y, float& w, float& h) const {
    if (shelf) {
        // Nearest last: the box in front.
        for (auto r = spineRects.rbegin(); r != spineRects.rend(); ++r) {
            if (r->index != index) continue;
            x = r->x0;
            y = r->y0;
            w = r->x1 - r->x0;
            h = r->y1 - r->y0;
            return w > 0 && h > 0;
        }
        return false;
    }
    if (index < 0 || index / kPerPage != page) return false;
    const int slot = index % kPerPage;
    x = static_cast<float>(TileX(slot));
    y = static_cast<float>(TileY(slot));
    w = static_cast<float>(Geo().tileW);
    h = static_cast<float>(Geo().tileH);
    return true;
}

int GuiGameGrid::Pages() const { return Count() == 0 ? 1 : (Count() + kPerPage - 1) / kPerPage; }

int GuiGameGrid::GetClicked() {
    const int c = clicked;
    clicked = -1;
    return c;
}

std::string GuiGameGrid::Fit(const std::string& text, int widthLimit) {
    const auto width_of = [&](const std::string& s) {
        measure->SetText(s.c_str());
        return measure->GetTextWidth();
    };
    if (text.empty() || width_of(text) <= widthLimit) return text;
    std::string cut = text;
    while (cut.size() > 1 && width_of(cut + "...") > widthLimit) cut.resize(CharBoundary(cut, cut.size() - 1));
    while (!cut.empty() && cut.back() == ' ') cut.pop_back();
    return cut + "...";
}

// Greedy word wrap, at spaces and after hyphens; a name without either
// (Japanese) is cut by characters.
std::vector<std::string> GuiGameGrid::Wrap(const std::string& text, int widthLimit, int lines) {
    const auto width_of = [&](const std::string& s) {
        measure->SetText(s.c_str());
        return measure->GetTextWidth();
    };
    std::vector<std::string> out;
    std::string rest = text;
    for (int l = 0; l < lines; ++l) {
        while (!rest.empty() && rest.front() == ' ') rest.erase(0, 1);
        if (rest.empty()) break;
        if (l == lines - 1) {
            out.push_back(Fit(rest, widthLimit));
            break;
        }
        // Words onto the line while they fit.
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
            // One long word: cut it by characters.
            std::size_t n = rest.size();
            while (n > 1 && width_of(rest.substr(0, n)) > widthLimit) n = CharBoundary(rest, n - 1);
            line = rest.substr(0, n);
            at = n;
        }
        out.push_back(line);
        rest = at < rest.size() ? rest.substr(at) : "";
    }
    return out;
}

void GuiGameGrid::Layout() {
    laidOut = true;
    const int size = covers ? kCoverTitleSize : kTitleSize;
    const int textWidth = covers ? Geo().tileW - 2 * kCoverTextLeft : Geo().tileW - 2 * kTextLeft;
    const int lineCount = covers ? 3 : 2;
    measure->SetFontSize(size);
    for (int i = 0; i < kPerPage; ++i) {
        Slot& s = slots[i];
        s.scale = 1.0f;
        if (page * kPerPage + i >= Count()) continue;
        const GridItem& item = (*items)[ItemAt(i)];
        // A name tile's disc (top right, from tileW - 46) keeps its name short of it.
        const bool disc = !covers && item.badge == "DISC";
        // A profile's name keeps short of its picture in the corner.
        const bool corner = item.profile && (channels || ProfilePictureOf(ItemAt(i)));
        const int pictureRoom = !covers && corner ? static_cast<int>(ProfilePictureSize()) + 2 : 0;
        const std::vector<std::string> lines =
            Wrap(item.title, disc ? Geo().tileW - 46 - kTextLeft - 4 : textWidth - pictureRoom, lineCount);
        for (int l = 0; l < 3; ++l) s.lines[l]->SetText(l < static_cast<int>(lines.size()) ? lines[l].c_str() : "");
        s.id->SetText(item.id.c_str());
        s.badge->SetText(item.badge.c_str());
        s.mods->SetText(item.mods ? "MODS" : "");
    }
}

void GuiGameGrid::TurnPage(int delta) {
    const int target = page + delta;
    if (target < 0 || target >= Pages()) return;
    const int cols = Cols();
    const int row = (focus % kPerPage) / cols;
    // Not with a game picked up: the old page's picture (or its names,
    // peeking) had it in it, and it stays where it is held.
    const bool peek = PagePeek() && carryFrom < 0;
    if (peek) {
        // The pages are one strip: it slides by a page, the column that
        // peeked in becoming the page's first (or last), the old page
        // drawn beside it with the names it had (Draw).
        std::swap(slots, leaving);
        for (Slot& s : leaving) s.scale = 1.0f;
        leavingPage = page;
    } else if (carryFrom < 0) {
        // The old page slides out the other way inside the rows' band,
        // while this one slides in (Draw).
        f32 vx, vy, vw, vh;
        Menu_VisibleArea(&vx, &vy, &vw, &vh);
        const float top = static_cast<float>(TileY(0) - 16);
        // Down past the lit game's name under the covers, all of it: an
        // edge across it showed the old name's top over the new one's foot.
        const float bottom = static_cast<float>(
            std::max(TileY(kPerPage - 1) + Geo().tileH + 30, covers ? CoverCaptionY() + kCaptionSize + 14 : 0));
        // Across the tiles' columns only (their lit edge included): the
        // search button above the last column and the page arrows stay
        // out, or the sliding picture took a copy of them along (a
        // tester saw the magnifier's ghost).
        const Geometry& g = Geo();
        const float left = std::max({vx, static_cast<float>(g.left - 12), static_cast<float>(ArrowX(-1) + 44)});
        const float right = std::min({vx + vw, static_cast<float>(g.left + cols * g.tileW + (cols - 1) * g.gapX + 12),
                                      static_cast<float>(ArrowX(1))});  // the right arrow and the search button start here
        riftwii::wii::transition::Begin(delta > 0 ? riftwii::wii::transition::Kind::PageForward
                                                  : riftwii::wii::transition::Kind::PageBack,
                                        riftwii::wii::transition::Rect{left, top, right - left, bottom - top});
    }
    page = target;
    // Enter the new page at the facing column of the same row.
    int slot = row * cols + (delta > 0 ? 0 : cols - 1);
    while (page * kPerPage + slot >= Count() && slot > 0) --slot;
    focus = page * kPerPage + slot;
    laidOut = false;
    // Peeking, the strip goes on from where it was (a turn during a turn),
    // at most two pages behind (DrawNeighbourPages draws three either side).
    slide = peek ? std::max(-2 * PageStride(), std::min(2 * PageStride(), slide + delta * PageStride()))
                 : delta * kSlideFrom;
    turning = delta;
    soundClick->Play();
}

int GuiGameGrid::SlotAt(int x, int y) const {
    if (PagePeek()) {
        // Beside the page's columns is the peeking pages' (their arrow):
        // a tile sliding in under a pointer resting there is not lit.
        const Geometry& g = Geo();
        if (x < g.left || x >= g.left + PageStride() - g.gapX) return -1;
    }
    for (int i = 0; i < kPerPage; ++i) {
        if (page * kPerPage + i >= Count()) break;
        if (x >= TileX(i) && x < TileX(i) + Geo().tileW && y >= TileY(i) && y < TileY(i) + Geo().tileH) return i;
    }
    return -1;
}

int GuiGameGrid::ArrowAt(int x, int y) const {
    if (PagePeek() && y >= TileY(0) && y < TileY(kPerPage - 1) + Geo().tileH) {
        // A neighbouring page's column is its arrow, from the screen's edge
        // to the page's own columns.
        const Geometry& g = Geo();
        if (page > 0 && x < g.left) return -1;
        if (page + 1 < Pages() && x >= g.left + Cols() * (g.tileW + g.gapX) - g.gapX) return 1;
    }
    if (y < ArrowY() || y >= ArrowY() + 44) return 0;
    if (page > 0 && x >= ArrowX(-1) && x < ArrowX(-1) + 44) return -1;
    if (page + 1 < Pages() && x >= ArrowX(1) && x < ArrowX(1) + 44) return 1;
    return 0;
}

void GuiGameGrid::DrawNameTile(int index, Slot& s, float x, float y, bool on, int alpha) {
    const int tileW = Geo().tileW, tileH = Geo().tileH;
    const GridItem& item = (*items)[index];
    if (PagePeek()) {
        // The texts are drawn at the grid's alpha times their own: as faded
        // as the tile, peeking or sliding along the strip. (Elsewhere they
        // stay as they were, whole while the tile fades.)
        const int gridAlpha = GetAlpha();
        const int textAlpha = gridAlpha > 0 ? std::min(255, alpha * 255 / gridAlpha) : 0;
        for (GuiText* t : {s.lines[0], s.lines[1], s.id, s.badge, s.mods}) t->SetAlpha(textAlpha);
    }
    if (channels && skin::channelCard9.data) {
        // A game without a banner in the Channels view: the channel's own
        // box (as the icons' and the empty places'), with its name in it.
        const float m = static_cast<float>(riftwii::kChannelFrameMargin);
        const float w = tileW * s.scale, h = tileH * s.scale;
        const float cx = x + tileW / 2.0f, cy = y + tileH / 2.0f;
        skin::DrawNine(on ? skin::channelCardOver9 : skin::channelCard9, cx - w / 2 - m, cy - h / 2 - m, w + 2 * m,
                       h + 2 * m, riftwii::kChannelFrameCorner, alpha);
    } else {
        DrawTilePicture(on ? skin::tileOver : skin::tile, x, y, alpha, s.scale);
    }
    // The game's colour along the bottom, inset from the round corners.
    const float grow = (s.scale - 1.0f);
    const float bx = x + 12 - grow * tileW / 2, bw = tileW - 24 + grow * tileW;
    const float by = y + tileH - 7 + grow * tileH / 2;
    Menu_DrawRectangle(bx, by, bw, 3, skin::WithAlpha(item.hue, alpha), 1);
    const float dx = -grow * (tileW / 2.0f - kTextLeft), dy = -grow * (tileH / 2.0f - 9);
    s.lines[0]->SetPosition(static_cast<int>(x + kTextLeft + dx), static_cast<int>(y + 9 + dy));
    s.lines[1]->SetPosition(static_cast<int>(x + kTextLeft + dx), static_cast<int>(y + 27 + dy));
    s.lines[0]->Draw();
    s.lines[1]->Draw();
    if (item.badge == "DISC")
        skin::Draw(skin::iconDisc, x + tileW - 46 - dx, y + 8 + dy, alpha, s.scale);
    // Source badge and MODS tag along the bottom line.
    const int baseY = static_cast<int>(y + tileH - 26 + grow * (tileH / 2.0f - 26));
    const int badgeW = s.badge->GetTextWidth() + 10;
    const int badgeX = static_cast<int>(x + tileW - kTextLeft - badgeW - dx);
    // The ID gives way to the MODS tag on a tile too narrow for both.
    const int idX = static_cast<int>(x + kTextLeft + dx);
    const int modsLeft = item.mods ? badgeX - (s.mods->GetTextWidth() + 10) - 5 : badgeX;
    if (idX + s.id->GetTextWidth() + 4 <= modsLeft) {
        s.id->SetPosition(idX, baseY);
        s.id->Draw();
    }
    Menu_DrawRectangle(badgeX, baseY - 1, badgeW, 15, skin::WithAlpha(skin::kBadge, alpha), 1);
    s.badge->SetPosition(badgeX + 5, baseY);
    s.badge->Draw();
    if (item.mods) {
        const int modsW = s.mods->GetTextWidth() + 10;
        const int modsX = badgeX - modsW - 5;
        Menu_DrawRectangle(modsX, baseY - 1, modsW, 15, skin::WithAlpha(skin::kAccentInk, alpha), 1);
        s.mods->SetPosition(modsX + 5, baseY);
        s.mods->Draw();
    }
}

bool GuiGameGrid::DrawChannelTile(int index, float x, float y, float scale, bool on, int alpha) {
    // The icon fills the tile inside its border, 4:3 across the
    // tile's width (its top and bottom cut off), with the tile's
    // round corners; scaled about the tile's centre. A widescreen
    // menu's wider tile stretches the icon across it, as the Wii
    // Menu does, and the player narrows back the panes the icon
    // flags to keep their shape (its logo).
    const float tileW = Geo().tileW * scale, tileH = Geo().tileH * scale;
    const float cx = x + Geo().tileW / 2.0f, cy = y + Geo().tileH / 2.0f;
    // The icon runs to the channel's rim, which is drawn over its
    // edge (the Wii Menu's), its shadow in the frame's margin.
    const float border = 1.0f;
    const float m = static_cast<float>(riftwii::kChannelFrameMargin);
    const auto frame = [&](const skin::Tex& t) {
        skin::DrawNine(t, cx - tileW / 2 - m, cy - tileH / 2 - m, tileW + 2 * m, tileH + 2 * m,
                       riftwii::kChannelFrameCorner, alpha);
    };
    const auto rim = [&]() { frame(on ? skin::channelFrameOver9 : skin::channelFrame9); };
    IconBox box;
    box.clipW = tileW - 2 * border;
    box.clipH = tileH - 2 * border;
    box.clipX = cx - box.clipW / 2;
    box.clipY = cy - box.clipH / 2;
    box.radius = std::max(0.0f, riftwii::kChannelRadius * skin::ChannelCorners() * scale - border);
    // The icon at its own size across the channel's width (a
    // widescreen menu's units stretch it as the Wii Menu does), on
    // whole pixels so each texel is one pixel; lit, it grows with
    // the tile.
    box.w = kIconPixelsW * IconScale() / PixelsPerUnitX() * scale;
    box.h = kIconPixelsH * IconScale() * scale;
    box.x = cx - box.w / 2;
    box.y = cy - box.h / 2;
    if (scale == 1.0f) {
        box.x = SnapX(box.x);
        box.y = SnapY(box.y);
    }
    box.alpha = alpha;
    if (iconDrawer(index, box)) {
        rim();
        return true;
    }
    if ((*items)[index].badge == "DISC" && skin::iconDisc.data) {
        // The disc drive, as the Wii Menu's Disc Channel: its disc,
        // large in the middle of a plain channel; its name on the
        // label when lit, as the others'.
        frame(on ? skin::channelCardOver9 : skin::channelCard9);
        const float k = std::min(box.clipH * 0.70f, box.clipW * 0.5f) / skin::iconDisc.h;
        skin::Draw(skin::iconDisc, cx - skin::iconDisc.w / 2.0f, cy - skin::iconDisc.h / 2.0f, alpha, k);
        return true;
    }
    return false;
}

void GuiGameGrid::DrawCoverTile(int i, bool on, int alpha) {
    const int tileW = Geo().tileW, tileH = Geo().tileH;
    const GridItem& item = (*items)[ItemAt(i)];
    Slot& s = slots[i];
    const float x = TileX(i), y = TileY(i);
    // A point of the tile, scaled about its centre with the tile.
    const auto at_x = [&](float off) { return static_cast<int>(x + tileW / 2.0f + (off - tileW / 2.0f) * s.scale); };
    const auto at_y = [&](float off) { return static_cast<int>(y + tileH / 2.0f + (off - tileH / 2.0f) * s.scale); };
    // The stored cover and its frame, drawn at CoverScale() about the tile's middle.
    const float kCoverScale = CoverScale();
    const float cx = x + tileW / 2.0f, cy = y + tileH / 2.0f;
    const skin::Tex& frame = on ? skin::coverTileOver : skin::coverTile;
    skin::Draw(frame, cx - frame.w / 2.0f, cy - frame.h / 2.0f, alpha, s.scale * kCoverScale);
    const u8* cover = riftwii::wii::CoverTexture(item.id);
    if (cover) {
        skin::DrawRgb5a3(cover, riftwii::kCoverWidth, riftwii::kCoverHeight, cx - riftwii::kCoverWidth / 2.0f,
                         cy - riftwii::kCoverHeight / 2.0f, alpha, s.scale * kCoverScale);
    } else if (item.badge == "DISC" && skin::discCase.data) {
        // The disc drive's own box, at a cover's size.
        const float k = static_cast<float>(tileH) / skin::discCase.h;
        skin::Draw(skin::discCase, x + tileW / 2.0f - skin::discCase.w / 2.0f, y + tileH / 2.0f - skin::discCase.h / 2.0f,
                   alpha, s.scale * k);
    } else {
        // No cover: the name, the ID and the game's colour, as a name tile.
        for (int l = 0; l < 3; ++l) {
            s.lines[l]->SetPosition(at_x(kCoverTextLeft), at_y(8 + l * 16));
            s.lines[l]->Draw();
        }
        s.id->SetPosition(at_x(kCoverTextLeft), at_y(tileH - 40));
        s.id->Draw();
        // The disc drive has no box art: a disc instead.
        if (item.badge == "DISC") skin::Draw(skin::iconDisc, at_x(20), at_y(44), alpha, s.scale);
        Menu_DrawRectangle(at_x(8), at_y(tileH - 6), (tileW - 16) * s.scale, 3, skin::WithAlpha(item.hue, alpha), 1);
    }
    // Source badge and MODS tag along the bottom, over the cover. The disc
    // drive's box says what it is: no DISC badge over its disc.
    if (item.badge == "DISC" && !cover && skin::discCase.data) return;
    const int baseY = at_y(tileH - 22);
    const int badgeW = s.badge->GetTextWidth() + 10;
    const int badgeX = at_x(tileW - 5) - badgeW;
    Menu_DrawRectangle(badgeX, baseY - 1, badgeW, 15, skin::WithAlpha(skin::WithAlpha(skin::kBadge, 235), alpha), 1);
    s.badge->SetPosition(badgeX + 5, baseY);
    s.badge->Draw();
    if (item.mods) {
        const int modsW = s.mods->GetTextWidth() + 10;
        const int modsX = at_x(5);
        Menu_DrawRectangle(modsX, baseY - 1, modsW, 15, skin::WithAlpha(skin::kAccentInk, alpha), 1);
        s.mods->SetPosition(modsX + 5, baseY);
        s.mods->Draw();
    }
}

// A profile's picture: a quarter or so of the tile, in its top right
// corner, so the game's own icon, cover or name still shows which game it is.
float GuiGameGrid::ProfilePictureSize() const {
    if (covers) return Geo().tileW * 0.46f;
    return Geo().tileH * (channels ? 0.46f : 0.40f);
}

const u8* GuiGameGrid::ProfilePictureOf(int index) const {
    if (!pictureSource || pictureSide <= 0 || index < 0 || index >= Count() || !(*items)[index].profile) return nullptr;
    return pictureSource(index);
}

void GuiGameGrid::DrawProfilePicture(int slot, float scale, int alpha) {
    // No picture: in Channels, where the tile shows the game's own banner,
    // the Mods icon in the corner tells the profile's tile from the game's;
    // in Names and Covers the tile's name and MODS tag say it, and a Mods
    // icon there was taken for something else on a Wii.
    const u8* picture = ProfilePictureOf(ItemAt(slot));
    if (!picture && !channels) return;
    const float tileW = Geo().tileW * scale, tileH = Geo().tileH * scale;
    const float cx = TileX(slot) + Geo().tileW / 2.0f, cy = TileY(slot) + Geo().tileH / 2.0f;
    const float size = ProfilePictureSize() * scale;
    const float inset = (covers ? 3.0f : 6.0f) * scale;
    const float x = cx + tileW / 2 - inset - size, y = cy - tileH / 2 + inset;
    // On a card of its own, as a sticker on the tile: a soft shadow, a
    // light rim, the picture (or the Mods icon) inside it.
    const float rim = 2.0f * scale;
    Menu_DrawRectangle(x - rim + 1, y - rim + 2, size + 2 * rim, size + 2 * rim, (GXColor){0, 0, 0, static_cast<u8>(alpha * 70 / 255)}, 1);
    Menu_DrawRectangle(x - rim, y - rim, size + 2 * rim, size + 2 * rim, skin::WithAlpha(skin::kBadge, alpha), 1);
    if (!picture) {
        Menu_DrawRectangle(x, y, size, size, skin::WithAlpha(skin::kAccentInk, alpha), 1);
        const skin::Tex& icon = skin::iconMods;
        if (icon.data && icon.w > 0 && icon.h > 0) {
            const float iconScale = size * 0.72f / std::max(icon.w, icon.h);
            skin::Draw(icon, x + size / 2 - icon.w / 2.0f, y + size / 2 - icon.h / 2.0f, alpha, iconScale);
        }
        return;
    }
    const float k = size / pictureSide;
    // Menu_DrawImg scales about the picture's middle: put it on the square's.
    Menu_DrawImg(x + size / 2 - pictureSide / 2.0f, y + size / 2 - pictureSide / 2.0f, static_cast<u16>(pictureSide),
                 static_cast<u16>(pictureSide), const_cast<u8*>(picture), 0, k, k, static_cast<u8>(alpha));
}

void GuiGameGrid::DrawChannelLabel(int slot, int alpha) {
    const int shown = ItemAt(slot);
    if (shown != captionFor) {
        captionFor = shown;
        measure->SetFontSize(kChannelLabelSize);
        caption->SetFontSize(kChannelLabelSize);
        caption->SetText(Fit((*items)[shown].title, 220).c_str());
        measure->SetFontSize(kTitleSize);
    }
    // A pill under the tile, overlapping its foot, as the Wii Menu labels
    // a channel; inside a TV's overscan.
    const int textW = caption->GetTextWidth();
    const int boxW = textW + 28, boxH = 28;
    int x = TileX(slot) + Geo().tileW / 2 - boxW / 2;
    x = x < 32 ? 32 : x + boxW > 608 ? 608 - boxW : x;
    const int y = TileY(slot) + Geo().tileH - 8;
    const int m = riftwii::kLabelMargin;
    skin::DrawNine(skin::label9, x - m, y - m, boxW + 2 * m, boxH + 2 * m, riftwii::kLabelCorner, alpha);
    // The caption is centred on the screen's middle: move it to the box's.
    // The font's letters sit low in their line: one up looks centred.
    caption->SetPosition(x + boxW / 2 - screenwidth / 2, y + (boxH - kChannelLabelSize) / 2 - 2);
    caption->SetAlpha(alpha);
    caption->Draw();
}

// Channels only, as the Wii Menu's home screen: the other views' tiles
// fill the screen between the arrows.
bool GuiGameGrid::PagePeek() const {
    return pagePeek && channels && iconDrawer && skin::channelCard9.data;
}

// A page's columns and the gap after the last: the next page's first
// column stands one gap beyond this page's last, as the columns do.
float GuiGameGrid::PageStride() const { return static_cast<float>(Cols() * (Geo().tileW + Geo().gapX)); }

// Whole inside the page's columns; outside, dimmed by how far the tile's
// middle is out of them, to kPeekDim one column out (where a peeking
// column rests). A sliding column brightens as it comes in.
int GuiGameGrid::PeekAlpha(float x, int alpha) const {
    const Geometry& g = Geo();
    const float middle = x + g.tileW / 2.0f;
    const float pageLeft = static_cast<float>(g.left);
    const float pageRight = pageLeft + PageStride() - g.gapX;
    const float out = middle < pageLeft ? pageLeft - middle : middle > pageRight ? middle - pageRight : 0.0f;
    const float column = g.tileW / 2.0f + g.gapX;
    const float t = std::min(1.0f, out / column);
    // The side the pointer is on (its page's arrow lit) a little brighter.
    const int side = middle < pageLeft ? -1 : middle > pageRight ? 1 : 0;
    const float dim = side != 0 && side == arrowHover ? kPeekLit : kPeekDim;
    return static_cast<int>(alpha * (1.0f - (1.0f - dim) * t));
}

// The pages either side of the one shown, on the strip it is part of:
// up to three either way, as turns in quick succession leave the old
// pages sliding off. Only tiles on screen are drawn: at rest a column a side,
// at most six icons more than the page's twelve. Their icons are the
// ones the menu keeps loaded for the pages either side (rift_menu.cpp).
// Not lit or pointable; the column a side is that page's arrow.
void GuiGameGrid::DrawNeighbourPages(int alpha) {
    f32 vx, vy, vw, vh;
    Menu_VisibleArea(&vx, &vy, &vw, &vh);
    const Geometry& g = Geo();
    const float m = static_cast<float>(riftwii::kChannelFrameMargin);
    for (int p = page - 3; p <= page + 3; ++p) {
        if (p == page || p < 0 || p >= Pages()) continue;
        const float offset = (p - page) * PageStride();
        for (int i = 0; i < kPerPage; ++i) {
            // At the slots' own places: the glide is the page shown's.
            const float x = BaseX(i) + slide + offset, y = static_cast<float>(BaseY(i));
            if (x + g.tileW + m <= vx || x - m >= vx + vw) continue;
            const int a = PeekAlpha(x, alpha);
            const int index = ShownItem(p * kPerPage + i);
            if (index >= Count()) {
                // The page's empty places, as the page's own.
                skin::DrawNine(skin::channelCard9, x - m, y - m, g.tileW + 2 * m, g.tileH + 2 * m,
                               riftwii::kChannelFrameCorner, a * 110 / 255);
                continue;
            }
            if (DrawChannelTile(index, x, y, 1.0f, false, a)) continue;
            if (p == leavingPage) {
                // The page just left: its names as they were laid out.
                DrawNameTile(index, leaving[i], x, y, false, a);
                continue;
            }
            // A game without its icon on a page not laid out: its channel
            // and colour, its name when its page is shown.
            skin::DrawNine(skin::channelCard9, x - m, y - m, g.tileW + 2 * m, g.tileH + 2 * m,
                           riftwii::kChannelFrameCorner, a);
            Menu_DrawRectangle(x + 12, y + g.tileH - 7, g.tileW - 24, 3, skin::WithAlpha((*items)[index].hue, a), 1);
        }
    }
}

// The neighbouring pages' covers, read ahead in the background so turning
// finds them in memory: the next two the way the pages last turned, then
// the one behind.
void GuiGameGrid::PrefetchCovers() {
    int rank = 1;
    for (const int p : {page + turning, page + 2 * turning, page - turning}) {
        if (p >= 0 && p < Pages()) {
            for (int i = p * kPerPage; i < (p + 1) * kPerPage && i < Count(); ++i)
                riftwii::wii::CoverPrefetch((*items)[i].id, rank);
        }
        ++rank;
    }
}

// Room for every game's cover, and the loader told to read them all, from
// the page shown onwards: once read, no page waits for its covers. Again
// when the list changes (a filter, a rescan).
void GuiGameGrid::ReadAllCovers() {
    const std::string first = Count() > 0 ? (*items)[0].id : "", last = Count() > 0 ? (*items)[Count() - 1].id : "";
    if (items == readAllItems && Count() == readAllCount && first == readAllFirst && last == readAllLast) return;
    readAllItems = items;
    readAllCount = Count();
    readAllFirst = first;
    readAllLast = last;
    riftwii::wii::CoverReserve(Count() + 1);  // and the game page's
    std::vector<std::string> ids;
    ids.reserve(Count());
    for (int k = 0; k < Count(); ++k) ids.push_back((*items)[(page * kPerPage + k) % Count()].id);
    riftwii::wii::CoverReadAll(ids);
}

// An empty place in the grid (past the last game, or where a game held
// by the pointer would land), at the slot's own place.
void GuiGameGrid::DrawEmptyPlace(int i, int alpha) const {
    const float x = static_cast<float>(BaseX(i)) + slide, y = static_cast<float>(BaseY(i));
    const float tileW = static_cast<float>(Geo().tileW), tileH = static_cast<float>(Geo().tileH);
    if (channels && iconDrawer && skin::channelCard9.data) {
        // As the Wii Menu's empty channel: the very box a channel's
        // icon fills (its size, place and rim), faded and blank. The
        // Names view's empty tile, smaller and rounder, stood out
        // beside the icons.
        const float m = static_cast<float>(riftwii::kChannelFrameMargin);
        skin::DrawNine(skin::channelCard9, x - m, y - m, tileW + 2 * m, tileH + 2 * m, riftwii::kChannelFrameCorner,
                       alpha * 110 / 255);
        return;
    }
    if (covers) {
        skin::Draw(skin::coverTile, x + tileW / 2.0f - skin::coverTile.w / 2.0f,
                   y + tileH / 2.0f - skin::coverTile.h / 2.0f, alpha * 70 / 255, CoverScale());
        return;
    }
    DrawTilePicture(skin::tileEmpty.data ? skin::tileEmpty : skin::tile, x, y,
                    skin::tileEmpty.data ? alpha : alpha * 70 / 255, 1.0f);
}

int GuiGameGrid::ItemAt(int slot) const { return ShownItem(page * kPerPage + slot); }

int GuiGameGrid::ShownItem(int shown) const {
    if (carryFrom < 0 || shown >= Count()) return shown;
    if (shown == carryTo) return carryFrom;
    // The games between where it was and where it would land close up.
    if (carryFrom < carryTo && shown >= carryFrom && shown < carryTo) return shown + 1;
    if (carryTo < carryFrom && shown > carryTo && shown <= carryFrom) return shown - 1;
    return shown;
}

// Each tile starts from where its game was drawn last frame and glides to
// its slot; the game held by a pointer hangs from the pointer instead.
void GuiGameGrid::EaseShifts() {
    if (shiftPage != page) {
        // Another page: it slides in whole, nothing glides across.
        shiftPage = page;
        for (int i = 0; i < kPerPage; ++i) {
            shownItem[i] = -1;
            shiftX[i] = shiftY[i] = 0.0f;
        }
    }
    int was[kPerPage];
    float wasX[kPerPage], wasY[kPerPage];
    for (int i = 0; i < kPerPage; ++i) {
        was[i] = shownItem[i];
        wasX[i] = BaseX(i) + shiftX[i];
        wasY[i] = BaseY(i) + shiftY[i];
    }
    for (int i = 0; i < kPerPage; ++i) {
        const int item = page * kPerPage + i < Count() ? ItemAt(i) : -1;
        if (item != was[i]) {
            shiftX[i] = shiftY[i] = 0.0f;
            for (int j = 0; j < kPerPage && item >= 0; ++j) {
                if (was[j] != item) continue;
                shiftX[i] = wasX[j] - BaseX(i);
                shiftY[i] = wasY[j] - BaseY(i);
                break;
            }
            shownItem[i] = item;
            laidOut = false;
        }
        shiftX[i] *= 0.7f;
        shiftY[i] *= 0.7f;
        if (std::fabs(shiftX[i]) < 0.5f) shiftX[i] = 0.0f;
        if (std::fabs(shiftY[i]) < 0.5f) shiftY[i] = 0.0f;
    }
    const int carrySlot = carryFrom >= 0 && carryTo / kPerPage == page ? carryTo % kPerPage : -1;
    if (carrySlot >= 0 && carryPointer) {
        // Held by its middle, a little above the pointer's tip.
        shiftX[carrySlot] = carryX - Geo().tileW / 2.0f - BaseX(carrySlot) - slide;
        shiftY[carrySlot] = carryY - Geo().tileH / 2.0f - BaseY(carrySlot) - 6.0f;
    }
    if (!laidOut) Layout();
}

void GuiGameGrid::Draw() {
    if (!IsVisible()) return;
    if (!laidOut) Layout();
    const int alpha = GetAlpha();
    if (covers) ReadAllCovers();
    if (shelf) {
        DrawShelf(alpha);
        UpdateEffects();
        return;
    }
    // A turned page eases in from the side, fading up as it comes (with
    // the pages peeking, the strip slides and each tile's place says how
    // bright it is).
    const bool peek = PagePeek();
    if (slide != 0.0f) {
        slide *= 0.75f;
        if (slide > -0.5f && slide < 0.5f) slide = 0.0f;
    }
    const float away = peek ? 0.0f : (slide < 0.0f ? -slide : slide) / kSlideFrom;
    const int tileAlpha = static_cast<int>(alpha * (1.0f - 0.9f * away));
    EaseShifts();
    const int focusSlot = focus / kPerPage == page ? focus % kPerPage : -1;
    // A game picked up is the lit one, drawn last over the others.
    const int carrySlot = carryFrom >= 0 && carryTo / kPerPage == page ? carryTo % kPerPage : -1;
    const int lit = carrySlot >= 0 ? carrySlot : hover >= 0 ? hover : (AnyPointer() ? -1 : focusSlot);
    if (peek) DrawNeighbourPages(alpha);
    // Empty places first, then tiles, the lit one last so it sits on top.
    for (int i = 0; i < kPerPage; ++i) {
        if (page * kPerPage + i < Count()) continue;
        if (channels && iconDrawer && skin::channelCard9.data) {
            // As the Wii Menu's empty channel: the very box a channel's
            // icon fills (its size, place and rim), faded and blank. The
            // Names view's empty tile, smaller and rounder, stood out
            // beside the icons.
            const float m = static_cast<float>(riftwii::kChannelFrameMargin);
            const int a = peek ? PeekAlpha(TileX(i), alpha) : tileAlpha;
            skin::DrawNine(skin::channelCard9, TileX(i) - m, TileY(i) - m, Geo().tileW + 2 * m, Geo().tileH + 2 * m,
                           riftwii::kChannelFrameCorner, a * 110 / 255);
            continue;
        }
        if (covers)
            skin::Draw(skin::coverTile, TileX(i) + Geo().tileW / 2.0f - skin::coverTile.w / 2.0f,
                       TileY(i) + Geo().tileH / 2.0f - skin::coverTile.h / 2.0f, tileAlpha * 70 / 255, CoverScale());
        else DrawTilePicture(skin::tileEmpty.data ? skin::tileEmpty : skin::tile, TileX(i), TileY(i), skin::tileEmpty.data ? tileAlpha : tileAlpha * 70 / 255, 1.0f);
    }
    // Where a game held by the pointer would land: an empty place there.
    if (carrySlot >= 0 && carryPointer) DrawEmptyPlace(carrySlot, tileAlpha);
    const auto draw_tile_body = [&](int i) {
        Slot& s = slots[i];
        const bool on = i == lit;
        const float grown = i == carrySlot ? kCarryScale : on ? 1.06f : 1.0f;
        s.scale += (grown - s.scale) * 0.35f;
        if (!on && s.scale < 1.002f) s.scale = 1.0f;  // settled: a channel's icon back on whole pixels
        const int index = ItemAt(i);  // with a game picked up, the tiles shifted round it
        const int a = peek ? PeekAlpha(TileX(i), alpha) : tileAlpha;
        iconShown[i] = channels && iconDrawer && DrawChannelTile(index, TileX(i), TileY(i), s.scale, on, a);
        if (iconShown[i]) return;
        if (covers) DrawCoverTile(i, on, a);
        else DrawNameTile(index, s, TileX(i), TileY(i), on, a);
    };
    // A profile's picture over its tile, the item and alpha as the tile's
    // (a carried game shifts the others; the page peek fades the sides).
    const auto draw_tile = [&](int i) {
        draw_tile_body(i);
        if ((*items)[ItemAt(i)].profile) DrawProfilePicture(i, slots[i].scale, peek ? PeekAlpha(TileX(i), alpha) : tileAlpha);
    };
    for (int i = 0; i < kPerPage; ++i) {
        if (i == lit || page * kPerPage + i >= Count()) continue;
        draw_tile(i);
    }
    if (lit >= 0 && page * kPerPage + lit < Count()) draw_tile(lit);
    // Channels: the lit icon's name on a label across its tile's foot.
    if (channels && lit >= 0 && lit < kPerPage && iconShown[lit] && page * kPerPage + lit < Count())
        DrawChannelLabel(lit, peek ? PeekAlpha(TileX(lit), alpha) : tileAlpha);
    if (covers) {
        // The lit game's name (the focused one's while nothing is lit).
        const int shown = lit >= 0 ? ItemAt(lit) : focus;
        if (shown != captionFor) {
            captionFor = shown;
            measure->SetFontSize(kCaptionSize);
            caption->SetText(shown >= 0 && shown < Count() ? Fit((*items)[shown].title, 560).c_str() : "");
            measure->SetFontSize(kCoverTitleSize);
        }
        caption->SetPosition(0, CoverCaptionY());
        caption->Draw();
        PrefetchCovers();
    }
    if (page > 0) skin::Draw(arrowHover < 0 ? skin::arrowLeftOver : skin::arrowLeft, ArrowX(-1) - 2, ArrowY() - 2, alpha);
    if (page + 1 < Pages())
        skin::Draw(arrowHover > 0 ? skin::arrowRightOver : skin::arrowRight, ArrowX(1) - 2, ArrowY() - 2, alpha);
    UpdateEffects();
}

// The first game (in the view's order) whose name starts with the letter
// after (dir 1) or before (-1) the focused one's, wrapping round. Names that
// start with anything else come before A; the disc tile (when Home has it)
// has no letter.
int GuiGameGrid::LetterTarget(int dir) const {
    const auto letter = [&](int i) -> int {
        if ((*items)[static_cast<std::size_t>(i)].badge == "DISC") return -1;
        const std::string& title = (*items)[static_cast<std::size_t>(i)].title;
        // As Home sorts them: a leading "The" is skipped.
        const std::size_t at = riftwii::sort_name_start(title);
        const char c = at < title.size() ? title[at] : 0;
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        return c >= 'A' && c <= 'Z' ? c : '#';
    };
    const int n = Count();
    if (n <= 1) return focus;
    // From the disc tile, forward is the first letter and back the last.
    const int at = focus >= 0 && focus < n ? letter(focus) : -1;
    const int from = at >= 0 ? at : (dir > 0 ? 0 : 0x7F);
    int want = -1;  // the letter to go to
    int lowest = 0x7F, highest = -1;
    for (int i = 0; i < n; ++i) {
        const int l = letter(i);
        if (l < 0) continue;
        lowest = std::min(lowest, l);
        highest = std::max(highest, l);
        if (dir > 0 && l > from && (want < 0 || l < want)) want = l;
        if (dir < 0 && l < from && l > want) want = l;
    }
    if (want < 0) want = dir > 0 ? lowest : highest;
    for (int i = 0; i < n; ++i)
        if (letter(i) == want) return i;
    return focus;
}

bool GuiGameGrid::LetterKeys(GuiTrigger* t) {
    if (t->chan < 0 || t->chan > 3) return false;
    // A letter jump means something only in A to Z order (Home order:
    // Recently played or Most played jumped to games out of order).
    if (riftwii::wii::Settings().home_sort != "az") return false;
    const int ch = t->chan;
    const bool held = (t->wpad && (t->wpad->btns_h & (WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B))) ||
                      (t->pad.btns_h & PAD_TRIGGER_L) || (t->wiidrcdata.btns_h & WIIDRC_BUTTON_B);
    const auto go = [&](int target) {
        if (target == focus) return;
        Focus(target);
        shelfHover = -1;
        hover = -1;
        soundOver->Play();
    };
    if (!held) {
        if (letterHeld[ch] && !letterUsed[ch]) go(LetterTarget(1));
        letterHeld[ch] = false;
        return false;
    }
    if (!letterHeld[ch]) {
        letterHeld[ch] = true;
        letterUsed[ch] = false;
    }
    // L and R together are the screenshot combo (with Down): not letters,
    // and letting go of L afterwards is no letter either.
    if (t->pad.btns_h & PAD_TRIGGER_R) {
        letterUsed[ch] = true;
        return true;
    }
    if (t->Right() || t->Left()) {
        go(LetterTarget(t->Right() ? 1 : -1));
        letterUsed[ch] = true;
    } else if (t->Down() || t->Up()) {
        const int delta = t->Down() ? 1 : -1;
        if (shelf) ShelfStep(delta * kPerPage);
        else TurnPage(delta);
        letterUsed[ch] = true;
    }
    return true;
}

void GuiGameGrid::SetMovable(bool on, int fixed) {
    movable = on;
    fixedFirst = fixed < 0 ? 0 : fixed;
    if (!on && carryFrom >= 0 && !carryDropped) {
        carryFrom = carryTo = -1;
        laidOut = false;
    }
}

bool GuiGameGrid::TakeMove(int& from, int& to) {
    if (!carryDropped) return false;
    from = carryFrom;
    to = carryTo;
    // The list the caller now moves the game in has every index between
    // the two shifted by one: so are the ones the slots showed, or every
    // tile would glide.
    for (int& item : shownItem) {
        if (item < 0) continue;
        if (item == from) item = to;
        else if (from < to && item > from && item <= to) --item;
        else if (to < from && item >= to && item < from) ++item;
    }
    carryFrom = carryTo = -1;
    carryDropped = false;
    focus = to;
    laidOut = false;
    captionFor = -1;
    return true;
}

// The place a pointer at (x, y) would put a game down: the slot under it,
// or the nearest one when it is between or beside the tiles.
int GuiGameGrid::NearestIndex(int x, int y) const {
    const Geometry& g = Geo();
    const int rows = kPerPage / g.cols;
    const int column = std::clamp((x - g.left + g.gapX / 2) / (g.tileW + g.gapX), 0, g.cols - 1);
    const int row = std::clamp((y - g.top + g.gapY / 2) / (g.tileH + g.gapY), 0, rows - 1);
    const int index = page * kPerPage + row * g.cols + column;
    return std::clamp(index, fixedFirst, Count() - 1);
}

void GuiGameGrid::CarryTo(int index) {
    index = std::clamp(index, fixedFirst, Count() - 1);
    if (index == carryTo) return;
    carryTo = index;
    focus = index;
    soundOver->Play();
    // Without a pointer, the page follows the game to its new place.
    const int targetPage = index / kPerPage;
    if (targetPage != page) {
        const int delta = targetPage > page ? 1 : -1;
        TurnPage(delta);
        page = targetPage;
        focus = index;
    }
}

void GuiGameGrid::PutDown() {
    soundClick->Play();
    carryPointer = false;  // it glides from the pointer into its place
    edgeFrames = 0;
    if (carryTo == carryFrom) {
        carryFrom = carryTo = -1;
        laidOut = false;
        return;
    }
    carryDropped = true;  // shown moved until TakeMove
}

// Picking a game up, moving it and putting it down (SetMovable). True
// while that has the controller's buttons.
bool GuiGameGrid::CarryKeys(GuiTrigger* t) {
    if (carryDropped) return true;
    const int ch = t->chan;
    const auto letters_done = [&]() {
        // B let go after this is no letter (LetterKeys).
        if (ch < 0 || ch > 3) return;
        letterHeld[ch] = true;
        letterUsed[ch] = true;
    };
    if (carryFrom < 0) {
        if (!movable || shelf || !PressedA(t) || !HeldLetterB(t)) return false;
        const bool pointing = t->wpad && t->wpad->ir.valid;
        int index = focus;
        if (pointing) {
            const int slot = SlotAt(static_cast<int>(t->wpad->ir.x), static_cast<int>(t->wpad->ir.y));
            if (slot < 0) return false;
            index = page * kPerPage + slot;
            carryX = t->wpad->ir.x;
            carryY = t->wpad->ir.y;
        } else if (AnyPointer() || focus / kPerPage != page) {
            return false;
        }
        if (index < fixedFirst || index >= Count()) return false;
        carryFrom = carryTo = index;
        carryChan = ch;
        carryPointer = pointing;
        edgeFrames = 0;
        focus = index;
        hover = -1;
        arrowHover = 0;
        letters_done();
        soundClick->Play();
        return true;
    }
    // Another controller waits while one moves a game.
    if (ch != carryChan) return true;
    if (carryPointer) {
        if (t->wpad && t->wpad->ir.valid) {
            carryX = t->wpad->ir.x;
            carryY = t->wpad->ir.y;
            const int x = static_cast<int>(carryX), y = static_cast<int>(carryY);
            const Geometry& g = Geo();
            // Over a page arrow, or past the tiles' sides: that way's page.
            int side = ArrowAt(x, y);
            if (side == 0 && x < g.left - g.gapX && page > 0) side = -1;
            if (side == 0 && x > g.left + g.cols * g.tileW + (g.cols - 1) * g.gapX + g.gapX && page + 1 < Pages()) side = 1;
            arrowHover = side;
            if (side != 0 && ++edgeFrames >= kEdgeTurnFrames) {
                edgeFrames = kEdgeTurnFrames - kEdgeRepeatFrames;
                TurnPage(side);
            } else if (side == 0) {
                edgeFrames = 0;
            }
            const int target = NearestIndex(x, y);
            if (target != carryTo) {
                carryTo = target;
                soundOver->Play();
            }
            focus = carryTo;
        }
        // A pointer lost off the screen keeps the game where it was.
        if (!HeldA(t)) {
            arrowHover = 0;
            PutDown();
        }
        return true;
    }
    if (PressedA(t)) {
        PutDown();
        return true;
    }
    if (PressedB(t)) {
        // Back where it was, nothing moved.
        CarryTo(carryFrom);
        PutDown();
        letters_done();
        return true;
    }
    const int cols = Cols();
    const int slot = carryTo % kPerPage, column = slot % cols;
    const int rowStart = carryTo - column;
    int target = carryTo;
    if (PressedPage(t, -1) || PressedPage(t, 1)) {
        target = carryTo + (PressedPage(t, -1) ? -kPerPage : kPerPage);
    } else if (t->Right()) {
        // Past the right column: the next page, the same row's first place.
        target = column == cols - 1 ? rowStart + kPerPage : carryTo + 1;
    } else if (t->Left()) {
        target = column == 0 ? rowStart - kPerPage + cols - 1 : carryTo - 1;
    } else if (t->Down()) {
        if (slot + cols < kPerPage) target = carryTo + cols;
    } else if (t->Up()) {
        if (slot >= cols) target = carryTo - cols;
    }
    if (target == carryTo || target < fixedFirst || target / kPerPage >= Pages()) return true;
    // A place past the last game (an empty one) is the last game's.
    CarryTo(std::min(target, Count() - 1));
    return true;
}

void GuiGameGrid::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || Count() == 0) return;
    if (CarryKeys(t)) return;
    if (LetterKeys(t)) return;
    if (shelf) {
        UpdateShelf(t);
        return;
    }
    // Minus and Plus: the previous and the next page, pointing or not.
    if (PressedPage(t, -1) || PressedPage(t, 1)) {
        TurnPage(PressedPage(t, -1) ? -1 : 1);
        hover = -1;
        return;
    }
    if (t->wpad && t->wpad->ir.valid) {
        const int x = static_cast<int>(t->wpad->ir.x), y = static_cast<int>(t->wpad->ir.y);
        const int slot = SlotAt(x, y);
        if (slot != hover && slot >= 0) soundOver->Play();
        hover = slot;
        arrowHover = ArrowAt(x, y);
        if (slot >= 0) focus = page * kPerPage + slot;
        if (PressedA(t)) {
            if (arrowHover != 0) {
                TurnPage(arrowHover);
                hover = -1;
            } else if (slot >= 0) {
                clicked = page * kPerPage + slot;
                soundClick->Play();
            }
        }
        return;
    }
    if (AnyPointer()) return;  // another channel points; it decides
    hover = -1;
    arrowHover = 0;
    const int cols = Cols();
    const int slot = focus % kPerPage, column = slot % cols;
    int target = focus;
    // A page turned here moves the focus onto the new page itself: the
    // target follows it (it was the old page's, which kept the focus there
    // while the new page showed, nothing lit and the D-pad lost).
    if (t->Right()) {
        if (column == cols - 1 || focus + 1 >= Count()) {
            TurnPage(1);
            target = focus;
        } else {
            target = focus + 1;
        }
    } else if (t->Left()) {
        if (column == 0) {
            TurnPage(-1);
            target = focus;
        } else {
            target = focus - 1;
        }
    } else if (t->Down()) {
        if (slot + cols < kPerPage && focus + cols < Count()) target = focus + cols;
    } else if (t->Up()) {
        if (slot >= cols) target = focus - cols;
    }
    if (target != focus) {
        focus = target;
        soundOver->Play();
    }
    if (PressedA(t)) {
        clicked = focus;
        soundClick->Play();
    }
}
