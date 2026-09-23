// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_gamegrid.hpp"

#include "skin.hpp"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kTitleSize = 15;
constexpr int kTextLeft = 11;
constexpr int kTitleWidth = GuiGameGrid::kTileW - 2 * kTextLeft;
constexpr int kArrowY = GuiGameGrid::kTop + (3 * GuiGameGrid::kTileH + 2 * GuiGameGrid::kGap) / 2 - 22;
constexpr int kArrowLeftX = 2, kArrowRightX = 596;

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

int TileX(int slot) { return GuiGameGrid::kLeft + (slot % GuiGameGrid::kCols) * (GuiGameGrid::kTileW + GuiGameGrid::kGap); }
int TileY(int slot) { return GuiGameGrid::kTop + (slot / GuiGameGrid::kCols) * (GuiGameGrid::kTileH + GuiGameGrid::kGap); }

}  // namespace

GuiGameGrid::GuiGameGrid() {
    width = screenwidth;
    height = screenheight;
    selectable = true;
    for (Slot& s : slots) {
        s.line1 = MakeText(this, kTitleSize, skin::kInk);
        s.line2 = MakeText(this, kTitleSize, skin::kInk);
        s.id = MakeText(this, 11, skin::kInkDim);
        s.badge = MakeText(this, 10, skin::kInkSoft);
        s.mods = MakeText(this, 10, skin::kWhite);
    }
    measure = new GuiText(nullptr, kTitleSize, skin::kInk);
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiGameGrid::~GuiGameGrid() {
    for (Slot& s : slots) {
        delete s.line1;
        delete s.line2;
        delete s.id;
        delete s.badge;
        delete s.mods;
    }
    delete measure;
    delete soundOver;
    delete soundClick;
}

void GuiGameGrid::SetItems(const std::vector<GridItem>* list) {
    items = list;
    if (focus >= Count()) focus = Count() > 0 ? Count() - 1 : 0;
    page = focus / kPerPage;
    laidOut = false;
}

void GuiGameGrid::Focus(int index) {
    if (index < 0 || index >= Count()) index = 0;
    focus = index;
    if (page != focus / kPerPage) {
        page = focus / kPerPage;
        laidOut = false;
    }
}

int GuiGameGrid::Pages() const { return Count() == 0 ? 1 : (Count() + kPerPage - 1) / kPerPage; }

int GuiGameGrid::GetClicked() {
    const int c = clicked;
    clicked = -1;
    return c;
}

// Greedy word wrap into two lines of the tile's width; the second line is
// cut with an ellipsis when the name is longer still.
void GuiGameGrid::Layout() {
    laidOut = true;
    const auto width_of = [&](const std::string& s) {
        measure->SetText(s.c_str());
        return measure->GetTextWidth();
    };
    for (int i = 0; i < kPerPage; ++i) {
        Slot& s = slots[i];
        s.scale = 1.0f;
        const int index = page * kPerPage + i;
        if (index >= Count()) continue;
        const GridItem& item = (*items)[index];
        std::string line1, rest = item.title;
        // Words onto the first line while they fit.
        std::size_t at = 0;
        while (at < rest.size()) {
            std::size_t next = rest.find(' ', at);
            if (next == std::string::npos) next = rest.size();
            const std::string candidate = rest.substr(0, next);
            if (width_of(candidate) > kTitleWidth) break;
            line1 = candidate;
            at = next + 1;
        }
        if (line1.empty()) {
            // One long word: cut it by characters.
            std::size_t n = rest.size();
            while (n > 1 && width_of(rest.substr(0, n)) > kTitleWidth) --n;
            line1 = rest.substr(0, n);
            at = n;
        }
        std::string line2 = at < rest.size() ? rest.substr(at) : "";
        while (!line2.empty() && line2.front() == ' ') line2.erase(0, 1);
        if (!line2.empty() && width_of(line2) > kTitleWidth) {
            while (line2.size() > 1 && width_of(line2 + "...") > kTitleWidth) line2.pop_back();
            while (!line2.empty() && line2.back() == ' ') line2.pop_back();
            line2 += "...";
        }
        s.line1->SetText(line1.c_str());
        s.line2->SetText(line2.c_str());
        s.id->SetText(item.id.c_str());
        s.badge->SetText(item.badge.c_str());
        s.mods->SetText(item.mods ? "MODS" : "");
    }
}

void GuiGameGrid::TurnPage(int delta) {
    const int target = page + delta;
    if (target < 0 || target >= Pages()) return;
    const int column = (focus % kPerPage) % kCols, row = (focus % kPerPage) / kCols;
    page = target;
    // Enter the new page at the facing column of the same row.
    int slot = row * kCols + (delta > 0 ? 0 : kCols - 1);
    (void)column;
    while (page * kPerPage + slot >= Count() && slot > 0) --slot;
    focus = page * kPerPage + slot;
    laidOut = false;
    soundClick->Play();
}

int GuiGameGrid::SlotAt(int x, int y) const {
    for (int i = 0; i < kPerPage; ++i) {
        if (page * kPerPage + i >= Count()) break;
        if (x >= TileX(i) && x < TileX(i) + kTileW && y >= TileY(i) && y < TileY(i) + kTileH) return i;
    }
    return -1;
}

int GuiGameGrid::ArrowAt(int x, int y) const {
    if (y < kArrowY || y >= kArrowY + 44) return 0;
    if (page > 0 && x >= kArrowLeftX && x < kArrowLeftX + 44) return -1;
    if (page + 1 < Pages() && x >= kArrowRightX && x < kArrowRightX + 44) return 1;
    return 0;
}

void GuiGameGrid::Draw() {
    if (!IsVisible()) return;
    if (!laidOut) Layout();
    const int alpha = GetAlpha();
    const int focusSlot = focus / kPerPage == page ? focus % kPerPage : -1;
    const int lit = hover >= 0 ? hover : (AnyPointer() ? -1 : focusSlot);
    // Empty places first, then tiles, the lit one last so it sits on top.
    for (int i = 0; i < kPerPage; ++i) {
        if (page * kPerPage + i < Count()) continue;
        skin::Draw(skin::tile, TileX(i) - 7, TileY(i) - 7, alpha * 70 / 255);
    }
    const auto draw_tile = [&](int i) {
        const int index = page * kPerPage + i;
        const GridItem& item = (*items)[index];
        Slot& s = slots[i];
        const bool on = i == lit;
        s.scale += ((on ? 1.06f : 1.0f) - s.scale) * 0.35f;
        const float x = TileX(i), y = TileY(i);
        skin::Draw(on ? skin::tileOver : skin::tile, x - 7, y - 7, alpha, s.scale);
        // The game's colour along the bottom, inset from the round corners.
        const float grow = (s.scale - 1.0f);
        const float bx = x + 12 - grow * kTileW / 2, bw = kTileW - 24 + grow * kTileW;
        const float by = y + kTileH - 7 + grow * kTileH / 2;
        Menu_DrawRectangle(bx, by, bw, 3, skin::WithAlpha(item.hue, alpha), 1);
        const float dx = -grow * (kTileW / 2.0f - kTextLeft), dy = -grow * (kTileH / 2.0f - 9);
        s.line1->SetPosition(static_cast<int>(x + kTextLeft + dx), static_cast<int>(y + 9 + dy));
        s.line2->SetPosition(static_cast<int>(x + kTextLeft + dx), static_cast<int>(y + 27 + dy));
        s.line1->Draw();
        s.line2->Draw();
        // Source badge and MODS tag along the bottom line.
        const int baseY = static_cast<int>(y + kTileH - 26 + grow * (kTileH / 2.0f - 26));
        s.id->SetPosition(static_cast<int>(x + kTextLeft + dx), baseY);
        s.id->Draw();
        const int badgeW = s.badge->GetTextWidth() + 10;
        const int badgeX = static_cast<int>(x + kTileW - kTextLeft - badgeW - dx);
        Menu_DrawRectangle(badgeX, baseY - 1, badgeW, 15, skin::WithAlpha((GXColor){236, 236, 241, 255}, alpha), 1);
        s.badge->SetPosition(badgeX + 5, baseY);
        s.badge->Draw();
        if (item.mods) {
            const int modsW = s.mods->GetTextWidth() + 10;
            const int modsX = badgeX - modsW - 5;
            Menu_DrawRectangle(modsX, baseY - 1, modsW, 15, skin::WithAlpha(skin::kAccentInk, alpha), 1);
            s.mods->SetPosition(modsX + 5, baseY);
            s.mods->Draw();
        }
    };
    for (int i = 0; i < kPerPage; ++i) {
        if (i == lit || page * kPerPage + i >= Count()) continue;
        draw_tile(i);
    }
    if (lit >= 0 && page * kPerPage + lit < Count()) draw_tile(lit);
    if (page > 0) skin::Draw(arrowHover < 0 ? skin::arrowLeftOver : skin::arrowLeft, kArrowLeftX - 2, kArrowY - 2, alpha);
    if (page + 1 < Pages())
        skin::Draw(arrowHover > 0 ? skin::arrowRightOver : skin::arrowRight, kArrowRightX - 2, kArrowY - 2, alpha);
    UpdateEffects();
}

void GuiGameGrid::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || Count() == 0) return;
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
    const int slot = focus % kPerPage, column = slot % kCols;
    int target = focus;
    if (t->Right()) {
        if (column == kCols - 1 || focus + 1 >= Count()) TurnPage(1);
        else target = focus + 1;
    } else if (t->Left()) {
        if (column == 0) TurnPage(-1);
        else target = focus - 1;
    } else if (t->Down()) {
        if (slot + kCols < kPerPage && focus + kCols < Count()) target = focus + kCols;
    } else if (t->Up()) {
        if (slot >= kCols) target = focus - kCols;
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
