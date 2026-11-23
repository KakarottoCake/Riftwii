// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_tiles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "drawtrace.hpp"
#include "i18n.hpp"
#include "gui_motion.hpp"
#include "transition.hpp"
#include "video.h"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kShadow = 8;       // the tile textures' margin around the card
constexpr int kPad = 12;
constexpr int kIcon = 28;
constexpr u32 kWpadA = WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A;

bool AnyPointer() {
    for (int i = 0; i < 4; i++)
        if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
    return false;
}

bool Pressed(GuiTrigger* t, u32 wpad, u16 pad, u16 drc) {
    return (t->wpad && (t->wpad->btns_d & wpad)) || (t->pad.btns_d & pad) || (t->wiidrcdata.btns_d & drc);
}

// A tall tile has its icon at the top and its texts at the bottom; a low
// one has them side by side.
bool Tall(const TileSpec& s) { return s.h >= 100; }

}  // namespace

GuiTiles::GuiTiles() : chevron(">", 18, skin::kInkDim) {
    width = screenwidth;
    height = screenheight;
    selectable = true;
    chevron.SetParent(this);
    chevron.SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiTiles::~GuiTiles() {
    for (GuiText* t : label) delete t;
    for (GuiText* t : value) delete t;
    delete soundOver;
    delete soundClick;
}

void GuiTiles::SetTiles(const std::vector<TileSpec>* t) {
    tiles = t;
    Refresh();
}

void GuiTiles::Refresh() {
    while (static_cast<int>(label.size()) < Count()) {
        GuiText* l = new GuiText(nullptr, 20, skin::kInk);
        l->SetParent(this);
        l->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        label.push_back(l);
        GuiText* v = new GuiText(nullptr, 15, skin::kInkSoft);
        v->SetParent(this);
        v->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        value.push_back(v);
    }
    labelSize.resize(label.size(), 20);
    chevronShown.resize(label.size(), 0);
    if (static_cast<int>(lift.size()) != Count()) {
        lift.assign(Count(), 0.0f);
        swell.assign(Count(), riftwii::wii::HoverSwell{});
    }
    if (focus >= Count()) focus = Count() > 0 ? Count() - 1 : 0;
    dirty = true;
}

void GuiTiles::Select(int index) {
    if (index >= 0 && index < Count()) focus = index;
}

int GuiTiles::GetClicked() {
    const int c = clicked;
    clicked = -1;
    return c;
}

int GuiTiles::TileAt(int x, int y) const {
    for (int i = 0; i < Count(); ++i) {
        const TileSpec& s = (*tiles)[i];
        if (x >= s.x && x < s.x + s.w && y >= s.y && y < s.y + s.h) return i;
    }
    return -1;
}

// The tile the D-pad goes to from `from`: of those whose middle lies that
// way, a tile in line with this one (overlapping it across the way of
// travel) always wins over one off to the side, and the nearest wins within
// each group. A plain distance score sent Cheats, Right, to Play (wide, on
// the row above, so its middle is close) instead of to Saves beside it.
int GuiTiles::Neighbour(int from, int dx, int dy) const {
    const TileSpec& a = (*tiles)[from];
    const float ax = a.x + a.w / 2.0f, ay = a.y + a.h / 2.0f;
    int best = -1;
    float bestScore = 0;
    bool bestInLine = false;
    for (int i = 0; i < Count(); ++i) {
        if (i == from) continue;
        const TileSpec& b = (*tiles)[i];
        const float bx = b.x + b.w / 2.0f, by = b.y + b.h / 2.0f;
        const float along = dx != 0 ? (bx - ax) * dx : (by - ay) * dy;
        if (along <= 4) continue;
        // The gap across the way of travel: 0 when the two overlap there.
        float across;
        if (dx != 0) across = std::max(0.0f, std::max(static_cast<float>(b.y - (a.y + a.h)), static_cast<float>(a.y - (b.y + b.h))));
        else across = std::max(0.0f, std::max(static_cast<float>(b.x - (a.x + a.w)), static_cast<float>(a.x - (b.x + b.w))));
        // Among tiles in line, the one whose middle is closest across.
        const float centre = dx != 0 ? std::fabs(by - ay) : std::fabs(bx - ax);
        const float score = along + across * 4.0f + centre * 0.25f;
        const bool inLine = across == 0.0f;
        const bool better = best < 0 || (inLine && !bestInLine) || (inLine == bestInLine && score < bestScore);
        if (better) {
            best = i;
            bestScore = score;
            bestInLine = inLine;
        }
    }
    return best;
}

// Where a tile's icon and texts go, and how big its name is drawn.
void GuiTiles::Layout(int i) {
    const TileSpec& s = (*tiles)[i];
    label[i]->SetText(s.label.c_str());
    label[i]->SetFontSize(s.primary ? 26 : 20);
    label[i]->SetColor(s.dim ? skin::kInkDim : s.primary ? skin::kAccentInk : skin::kInk);
    value[i]->SetText(s.value.c_str());
    value[i]->SetColor(s.dim ? skin::kInkDim : s.warn ? skin::kWarn : s.on ? skin::kAccentInk : skin::kInkSoft);
    const bool hasValue = !s.value.empty();
    int textX, labelY;
    if (Tall(s)) {
        textX = s.x + kPad;
        labelY = s.y + s.h - kPad - (hasValue ? 44 : 24);
    } else {
        textX = s.x + kPad + (s.icon ? kIcon + 9 : 0);
        labelY = s.y + (s.h - (hasValue ? 42 : (s.primary ? 28 : 22))) / 2;
        // A name alone: the font's letters sit low in their line, so it is
        // raised to look centred on the tile and its icon (About, Exit).
        if (!hasValue) labelY -= 3;
        // A name over its value sat 2 to 3 pixels low by the same measure
        // (Cheats over Off on the game page): raised too.
        else labelY -= 2;
    }
    const int chevronRoom = s.opensPage ? 18 : 0;
    int room = s.x + s.w - kPad - textX - chevronRoom;
    // A long name (a translation) steps down to 15 points before "..." cuts it.
    int size = s.primary ? 26 : 20;
    for (; size > 15; --size) {
        label[i]->SetFontSize(size);
        if (label[i]->GetTextWidth() <= room) break;
    }
    // A name that still doesn't fit beside the chevron (French "Codes de
    // triche" at 4:3) keeps its words: the chevron gives way.
    chevronShown[i] = s.opensPage;
    if (s.opensPage && label[i]->GetTextWidth() > room) {
        chevronShown[i] = 0;
        room += chevronRoom;
    }
    label[i]->SetFontSize(size);
    labelSize[i] = size;
    label[i]->SetMaxWidth(room);
    value[i]->SetMaxWidth(room);
    // A long value (a theme's name, a translation) keeps its size and
    // "..." cuts it: smaller letters on one tile looked out of place.
    value[i]->SetFontSize(15);
    label[i]->SetPosition(textX, labelY);
    value[i]->SetPosition(textX, labelY + (s.primary ? 28 : 23));
}

// One long name (a translation) shrinks every tile's name alike (the
// primary tile's keeps its own size): side by side, a tile in smaller
// letters than its neighbours looked out of place.
void GuiTiles::EvenTextSizes() {
    int labelMin = 20;
    for (int i = 0; i < Count(); ++i) {
        const TileSpec& s = (*tiles)[i];
        if (!s.primary && !s.label.empty()) labelMin = std::min(labelMin, labelSize[i]);
    }
    for (int i = 0; i < Count(); ++i)
        if (!(*tiles)[i].primary) label[i]->SetFontSize(labelMin);
}

void GuiTiles::Draw() {
    if (!IsVisible() || !tiles) return;
    riftwii::wii::DrawTrace trace("the page's tiles");
    const int alpha = GetAlpha();
    if (dirty) {
        dirty = false;
        for (int i = 0; i < Count(); ++i) Layout(i);
        EvenTextSizes();
    }
    pressScale = riftwii::wii::transition::PressScale(pressElapsed);
    const int lit = hover >= 0 ? hover : (AnyPointer() ? -1 : focus);
    for (int i = 0; i < Count(); ++i) {
        const TileSpec& s = (*tiles)[i];
        float& l = lift[i];
        l = std::clamp(swell[i].Advance(i == lit), 0.0f, 1.0f);
        if (s.hidden) continue;
        // Lit, the tile keeps its size and its ring fades in (and pulses).
        // A tile that grew stuck out past the tiles in line with it: the
        // lit Play 8 to 10 pixels past the column under it, a lit Cheats
        // past Options, the grid's edges no longer straight.
        Menu_PushCamera(PressedScale(i), s.x + s.w / 2.0f, s.y + s.h / 2.0f);
        const float w = s.w, h = s.h;
        f32 safeX, safeW;
        Menu_SafeArea(&safeX, &safeW);
        // Kept inside the overscan, its shadow too.
        const float safeLeft = safeX + safeW * 0.05f + kShadow;
        const float safeRight = safeX + safeW * 0.95f - kShadow;
        const float x = std::clamp(static_cast<float>(s.x), safeLeft, safeRight - w);
        const float y = std::clamp(static_cast<float>(s.y), 24.0f + kShadow, 456.0f - kShadow - h);
        const skin::Tex& base = s.primary ? skin::tile9Primary : skin::tile9;
        const skin::Tex& over = s.primary ? skin::tile9PrimaryOver : skin::tile9Over;
        const int tileAlpha = s.dim ? alpha * 3 / 4 : alpha;
        skin::DrawNine(base, x - kShadow, y - kShadow, w + 2 * kShadow, h + 2 * kShadow, skin::kTileCorner, tileAlpha);
        if (l > 0.0f)
            skin::DrawNine(over, x - kShadow, y - kShadow, w + 2 * kShadow, h + 2 * kShadow, skin::kTileCorner,
                           static_cast<int>(tileAlpha * l * riftwii::wii::transition::Pulse()));
        if (s.primary && i == lit)
            riftwii::wii::DrawButtonGlint(x, y, w, h, 16, tileAlpha);
        if (s.icon && s.icon->data) {
            // An icon with no words (the game page's Favourite star beside
            // the disc) sits in the middle of its tile.
            const bool iconOnly = s.label.empty() && s.value.empty();
            const float iconX = iconOnly ? s.x + (s.w - kIcon) / 2.0f : s.x + kPad;
            const float iconY = Tall(s) && !iconOnly ? s.y + kPad : s.y + (s.h - kIcon) / 2.0f;
            skin::Draw(*s.icon, iconX, iconY, s.dim ? alpha / 2 : alpha);
        }
        // The lit tile's name scrolls when it does not fit; the others end in "...".
        label[i]->SetScroll(i == lit ? SCROLL::HORIZONTAL : SCROLL::NONE);
        label[i]->Draw();
        if (!s.value.empty()) value[i]->Draw();
        if (chevronShown[i]) {
            // The page's ink: the softer one came to 2:1 on composite.
            chevron.SetColor(s.dim ? skin::kInkDim : skin::kInk);
            chevron.SetPosition(s.x + s.w - kPad - 10, s.y + (s.h - 22) / 2 - 2);
            chevron.Draw();
        }
        Menu_PopCamera();
    }
    UpdateEffects();
}

void GuiTiles::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || Count() == 0) return;
    const bool pointing = t->wpad && t->wpad->ir.valid;
    const bool a = Pressed(t, kWpadA, PAD_BUTTON_A, WIIDRC_BUTTON_A);
    if (pointing) {
        const int at = TileAt(static_cast<int>(t->wpad->ir.x), static_cast<int>(t->wpad->ir.y));
        if (at != hover && at >= 0) soundOver->Play();
        hover = at;
        if (at >= 0) focus = at;
        if (a && at >= 0) {
            clicked = at;
            pressedTile = at;
            pressElapsed = 0;
            soundClick->Play();
        }
        return;
    }
    // The other controllers (and a Wii Remote not pointing) are asked too:
    // only the one a press comes from moves the focus.
    if (AnyPointer()) return;
    hover = -1;
    int dx = 0, dy = 0;
    if (t->Right()) dx = 1;
    else if (t->Left()) dx = -1;
    else if (t->Down()) dy = 1;
    else if (t->Up()) dy = -1;
    if (dx != 0 || dy != 0) {
        const int next = Neighbour(focus, dx, dy);
        if (next >= 0) {
            focus = next;
            soundOver->Play();
        }
    }
    if (a) {
        clicked = focus;
        pressedTile = focus;
        pressElapsed = 0;
        soundClick->Play();
    }
}
