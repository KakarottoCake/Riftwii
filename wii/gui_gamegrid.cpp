// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_gamegrid.hpp"
#include "transition.hpp"

#include <algorithm>

#include "covers.hpp"
#include "menutheme.hpp"
#include "riftwii/coverart.hpp"
#include "skin.hpp"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kTitleSize = 15;       // names: the tile's two lines
constexpr int kCoverTitleSize = 13;  // covers: a tile without a cover
constexpr int kCaptionSize = 18;     // covers: the lit game's name
constexpr int kCaptionY = 266;
constexpr int kShelfCaptionY = 276;  // under the shelf (wii/gui_shelf.cpp)
constexpr int kChannelLabelSize = 15;  // channels: the lit icon's name
constexpr int kTextLeft = 11;
constexpr int kCoverTextLeft = 6;
// The page arrows (36 across in a 48 canvas) sit 14 in from the screen's
// edges, inside what a TV's overscan (the Wii U's especially) crops.
constexpr int kArrowLeftX = 10, kArrowRightX = 586;

// Names: 4x3 wide tiles. Covers: 6x2 at the stored cover size, the name
// line under them. Both between the arrows.
constexpr int kNameCols = 4;
constexpr int kCoverCols = 6;

// A turned page slides in from this far to the side, easing in.
constexpr float kSlideFrom = 150.0f;

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
    for (Slot& s : slots) {
        for (GuiText*& line : s.lines) line = MakeText(this, kTitleSize, skin::kInk);
        s.id = MakeText(this, 11, skin::kInkDim);
        s.badge = MakeText(this, 10, skin::kInkSoft);
        s.mods = MakeText(this, 10, skin::kTextOnAccent);
    }
    measure = new GuiText(nullptr, kTitleSize, skin::kInk);
    caption = new GuiText(nullptr, kCaptionSize, skin::kInk);
    caption->SetParent(this);
    caption->SetAlignment(ALIGN_H::CENTRE, ALIGN_V::TOP);
    caption->SetPosition(0, kCaptionY);
    spineText = new GuiText(nullptr, 13, GXColor{40, 40, 48, 255});
    spineText->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiGameGrid::~GuiGameGrid() {
    for (Slot& s : slots) {
        for (GuiText* line : s.lines) delete line;
        delete s.id;
        delete s.badge;
        delete s.mods;
    }
    delete measure;
    delete caption;
    delete spineText;
    delete soundOver;
    delete soundClick;
}

const GuiGameGrid::Geometry& GuiGameGrid::Geo() const {
    static const Geometry names = {kNameCols, 124, 84, 57, 20, 10, 12};
    static const Geometry coverGrid = {kCoverCols, riftwii::kCoverWidth, riftwii::kCoverHeight, 58, 16, 9, 14};
    return covers ? coverGrid : names;
}

int GuiGameGrid::TileX(int slot) const {
    return Geo().left + (slot % Geo().cols) * (Geo().tileW + Geo().gapX) + static_cast<int>(slide);
}
int GuiGameGrid::TileY(int slot) const { return Geo().top + (slot / Geo().cols) * (Geo().tileH + Geo().gapY); }

int GuiGameGrid::ArrowY() const {
    const int rows = kPerPage / Geo().cols;
    return Geo().top + (rows * Geo().tileH + (rows - 1) * Geo().gapY) / 2 - 22;
}

void GuiGameGrid::SetItems(const std::vector<GridItem>* list) {
    items = list;
    if (focus >= Count()) focus = Count() > 0 ? Count() - 1 : 0;
    page = focus / kPerPage;
    laidOut = false;
    captionFor = -1;
}

void GuiGameGrid::SetCovers(bool on) {
    if (covers == on) return;
    covers = on;
    for (Slot& s : slots) {
        for (GuiText* line : s.lines) line->SetFontSize(covers ? kCoverTitleSize : kTitleSize);
    }
    laidOut = false;
    captionFor = -1;
}

void GuiGameGrid::CoverArrived(const std::string& id) { riftwii::wii::ForgetCover(id); }

void GuiGameGrid::SetChannels(bool on, IconDrawer icon) {
    channels = on;
    iconDrawer = on ? std::move(icon) : IconDrawer();
    captionFor = -1;
    if (channels) SetCovers(false);
}

void GuiGameGrid::SetShelf(bool on) {
    if (shelf == on) return;
    shelf = on;
    poses.clear();
    spineFit.clear();
    captionFor = -1;
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
        const int index = page * kPerPage + i;
        if (index >= Count()) continue;
        const GridItem& item = (*items)[index];
        // A name tile's disc (top right, from tileW - 46) keeps its name short of it.
        const bool disc = !covers && item.badge == "DISC";
        const std::vector<std::string> lines =
            Wrap(item.title, disc ? Geo().tileW - 46 - kTextLeft - 4 : textWidth, lineCount);
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
    // The old page slides out the other way inside the rows' band, while
    // this one slides in (Draw).
    {
        f32 vx, vy, vw, vh;
        Menu_VisibleArea(&vx, &vy, &vw, &vh);
        const float top = static_cast<float>(TileY(0) - 16);
        const float bottom = static_cast<float>(TileY(kPerPage - 1) + Geo().tileH + 30);
        riftwii::wii::transition::Begin(delta > 0 ? riftwii::wii::transition::Kind::PageForward
                                                  : riftwii::wii::transition::Kind::PageBack,
                                        riftwii::wii::transition::Rect{vx, top, vw, bottom - top});
    }
    page = target;
    // Enter the new page at the facing column of the same row.
    int slot = row * cols + (delta > 0 ? 0 : cols - 1);
    while (page * kPerPage + slot >= Count() && slot > 0) --slot;
    focus = page * kPerPage + slot;
    laidOut = false;
    slide = delta * kSlideFrom;
    turning = delta;
    soundClick->Play();
}

int GuiGameGrid::SlotAt(int x, int y) const {
    for (int i = 0; i < kPerPage; ++i) {
        if (page * kPerPage + i >= Count()) break;
        if (x >= TileX(i) && x < TileX(i) + Geo().tileW && y >= TileY(i) && y < TileY(i) + Geo().tileH) return i;
    }
    return -1;
}

int GuiGameGrid::ArrowAt(int x, int y) const {
    if (y < ArrowY() || y >= ArrowY() + 44) return 0;
    if (page > 0 && x >= kArrowLeftX && x < kArrowLeftX + 44) return -1;
    if (page + 1 < Pages() && x >= kArrowRightX && x < kArrowRightX + 44) return 1;
    return 0;
}

void GuiGameGrid::DrawNameTile(int i, bool on, int alpha) {
    const int tileW = Geo().tileW, tileH = Geo().tileH;
    const GridItem& item = (*items)[page * kPerPage + i];
    Slot& s = slots[i];
    const float x = TileX(i), y = TileY(i);
    skin::Draw(on ? skin::tileOver : skin::tile, x - 7, y - 7, alpha, s.scale);
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
    s.id->SetPosition(static_cast<int>(x + kTextLeft + dx), baseY);
    s.id->Draw();
    const int badgeW = s.badge->GetTextWidth() + 10;
    const int badgeX = static_cast<int>(x + tileW - kTextLeft - badgeW - dx);
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

void GuiGameGrid::DrawCoverTile(int i, bool on, int alpha) {
    const int tileW = Geo().tileW, tileH = Geo().tileH;
    const GridItem& item = (*items)[page * kPerPage + i];
    Slot& s = slots[i];
    const float x = TileX(i), y = TileY(i);
    // A point of the tile, scaled about its centre with the tile.
    const auto at_x = [&](float off) { return static_cast<int>(x + tileW / 2.0f + (off - tileW / 2.0f) * s.scale); };
    const auto at_y = [&](float off) { return static_cast<int>(y + tileH / 2.0f + (off - tileH / 2.0f) * s.scale); };
    skin::Draw(on ? skin::coverTileOver : skin::coverTile, x - 7, y - 7, alpha, s.scale);
    const u8* cover = riftwii::wii::CoverTexture(item.id);
    if (cover) {
        skin::DrawRgb5a3(cover, tileW, tileH, x, y, alpha, s.scale);
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
    // Source badge and MODS tag along the bottom, over the cover.
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

void GuiGameGrid::DrawChannelLabel(int slot, int alpha) {
    const int shown = page * kPerPage + slot;
    if (shown != captionFor) {
        captionFor = shown;
        measure->SetFontSize(kChannelLabelSize);
        caption->SetFontSize(kChannelLabelSize);
        caption->SetText(Fit((*items)[shown].title, 220).c_str());
        measure->SetFontSize(kTitleSize);
    }
    const int textW = caption->GetTextWidth();
    const int boxW = textW + 18, boxH = kChannelLabelSize + 10;
    int x = TileX(slot) + Geo().tileW / 2 - boxW / 2;
    x = x < 8 ? 8 : x + boxW > 632 ? 632 - boxW : x;
    const int y = TileY(slot) + Geo().tileH - 6;
    Menu_DrawRectangle(x - 1, y - 1, boxW + 2, boxH + 2, skin::WithAlpha(skin::kAccent, alpha), 1);
    Menu_DrawRectangle(x, y, boxW, boxH, skin::WithAlpha(skin::kBar, alpha), 1);
    // The caption is centred on the screen's middle: move it to the box's.
    caption->SetPosition(x + boxW / 2 - screenwidth / 2, y + 4);
    caption->SetAlpha(alpha);
    caption->Draw();
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
    // A turned page eases in from the side, fading up as it comes.
    if (slide != 0.0f) {
        slide *= 0.75f;
        if (slide > -0.5f && slide < 0.5f) slide = 0.0f;
    }
    const float away = (slide < 0.0f ? -slide : slide) / kSlideFrom;
    const int tileAlpha = static_cast<int>(alpha * (1.0f - 0.9f * away));
    const int focusSlot = focus / kPerPage == page ? focus % kPerPage : -1;
    const int lit = hover >= 0 ? hover : (AnyPointer() ? -1 : focusSlot);
    // Empty places first, then tiles, the lit one last so it sits on top.
    for (int i = 0; i < kPerPage; ++i) {
        if (page * kPerPage + i < Count()) continue;
        skin::Draw(covers ? skin::coverTile : skin::tile, TileX(i) - 7, TileY(i) - 7, tileAlpha * 70 / 255);
    }
    const auto draw_tile = [&](int i) {
        Slot& s = slots[i];
        const bool on = i == lit;
        s.scale += ((on ? 1.06f : 1.0f) - s.scale) * 0.35f;
        iconShown[i] = false;
        if (channels && iconDrawer) {
            // The icon fills the tile inside its border, 4:3 across the
            // tile's width (its top and bottom cut off), with the tile's
            // round corners; scaled about the tile's centre.
            const float tileW = Geo().tileW * s.scale, tileH = Geo().tileH * s.scale;
            const float cx = TileX(i) + Geo().tileW / 2.0f, cy = TileY(i) + Geo().tileH / 2.0f;
            const float border = (on ? 2.5f : 1.5f) * s.scale;
            skin::Draw(on ? skin::tileOver : skin::tile, TileX(i) - 7, TileY(i) - 7, tileAlpha, s.scale);
            IconBox box;
            box.clipW = tileW - 2 * border;
            box.clipH = tileH - 2 * border;
            box.clipX = cx - box.clipW / 2;
            box.clipY = cy - box.clipH / 2;
            box.radius = std::max(0.0f, 14.0f * riftwii::wii::MenuTheme().corners * s.scale - border);
            box.w = box.clipW;
            box.h = box.w * 3.0f / 4.0f;
            box.x = box.clipX;
            box.y = cy - box.h / 2;
            box.alpha = tileAlpha;
            if (iconDrawer(page * kPerPage + i, box)) {
                iconShown[i] = true;
                return;
            }
        }
        if (covers) DrawCoverTile(i, on, tileAlpha);
        else DrawNameTile(i, on, tileAlpha);
    };
    for (int i = 0; i < kPerPage; ++i) {
        if (i == lit || page * kPerPage + i >= Count()) continue;
        draw_tile(i);
    }
    if (lit >= 0 && page * kPerPage + lit < Count()) draw_tile(lit);
    // Channels: the lit icon's name on a label across its tile's foot.
    if (channels && lit >= 0 && lit < kPerPage && iconShown[lit] && page * kPerPage + lit < Count())
        DrawChannelLabel(lit, tileAlpha);
    if (covers) {
        // The lit game's name (the focused one's while nothing is lit).
        const int shown = lit >= 0 ? page * kPerPage + lit : focus;
        if (shown != captionFor) {
            captionFor = shown;
            measure->SetFontSize(kCaptionSize);
            caption->SetText(shown >= 0 && shown < Count() ? Fit((*items)[shown].title, 560).c_str() : "");
            measure->SetFontSize(kCoverTitleSize);
        }
        caption->Draw();
        PrefetchCovers();
    }
    if (page > 0) skin::Draw(arrowHover < 0 ? skin::arrowLeftOver : skin::arrowLeft, kArrowLeftX - 2, ArrowY() - 2, alpha);
    if (page + 1 < Pages())
        skin::Draw(arrowHover > 0 ? skin::arrowRightOver : skin::arrowRight, kArrowRightX - 2, ArrowY() - 2, alpha);
    UpdateEffects();
}

// The first game (in the view's order) whose name starts with the letter
// after (dir 1) or before (-1) the focused one's, wrapping round. Names that
// start with anything else come before A; the disc tile (0) has no letter.
int GuiGameGrid::LetterTarget(int dir) const {
    const auto letter = [&](int i) -> int {
        const std::string& title = (*items)[static_cast<std::size_t>(i)].title;
        const char c = title.empty() ? 0 : title[0];
        if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
        return c >= 'A' && c <= 'Z' ? c : '#';
    };
    const int n = Count();
    if (n <= 1) return focus;
    // From the disc tile, forward is the first letter and back the last.
    const int from = focus > 0 && focus < n ? letter(focus) : (dir > 0 ? 0 : 0x7F);
    int want = -1;  // the letter to go to
    int lowest = 0x7F, highest = -1;
    for (int i = 1; i < n; ++i) {
        const int l = letter(i);
        lowest = std::min(lowest, l);
        highest = std::max(highest, l);
        if (dir > 0 && l > from && (want < 0 || l < want)) want = l;
        if (dir < 0 && l < from && l > want) want = l;
    }
    if (want < 0) want = dir > 0 ? lowest : highest;
    for (int i = 1; i < n; ++i)
        if (letter(i) == want) return i;
    return focus;
}

bool GuiGameGrid::LetterKeys(GuiTrigger* t) {
    if (t->chan < 0 || t->chan > 3) return false;
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

void GuiGameGrid::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || Count() == 0) return;
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
    if (t->Right()) {
        if (column == cols - 1 || focus + 1 >= Count()) TurnPage(1);
        else target = focus + 1;
    } else if (t->Left()) {
        if (column == 0) TurnPage(-1);
        else target = focus - 1;
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
