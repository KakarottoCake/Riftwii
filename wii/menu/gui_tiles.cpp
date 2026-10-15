// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_tiles.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "i18n.hpp"
#include "video.h"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kShadow = 8;       // the tile textures' margin around the card
constexpr float kLiftScale = 0.035f;  // how much a lit tile grows
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

GuiTiles::GuiTiles() {
    width = screenwidth;
    height = screenheight;
    selectable = true;
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
    if (static_cast<int>(lift.size()) != Count()) lift.assign(Count(), 0.0f);
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
// way, the nearest, where a tile in line with this one (overlapping it
// across the way of travel) counts as nearer than one off to the side.
int GuiTiles::Neighbour(int from, int dx, int dy) const {
    const TileSpec& a = (*tiles)[from];
    const float ax = a.x + a.w / 2.0f, ay = a.y + a.h / 2.0f;
    int best = -1;
    float bestScore = 0;
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
        if (best < 0 || score < bestScore) {
            best = i;
            bestScore = score;
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
    value[i]->SetColor(s.dim ? skin::kInkDim : s.on ? skin::kAccentInk : skin::kInkSoft);
    const bool hasValue = !s.value.empty();
    int textX, labelY;
    if (Tall(s)) {
        textX = s.x + kPad;
        labelY = s.y + s.h - kPad - (hasValue ? 44 : 24);
    } else {
        textX = s.x + kPad + (s.icon ? kIcon + 9 : 0);
        labelY = s.y + (s.h - (hasValue ? 42 : (s.primary ? 28 : 22))) / 2;
    }
    const int room = s.x + s.w - kPad - textX;
    // A long name (a translation) steps down to 15 points before "..." cuts it.
    for (int size = s.primary ? 26 : 20; size >= 15; --size) {
        label[i]->SetFontSize(size);
        if (label[i]->GetTextWidth() <= room) break;
    }
    label[i]->SetMaxWidth(room);
    value[i]->SetMaxWidth(room);
    // A long value (a translation) steps down to 13 points before "..." cuts it.
    for (int size = 15; size >= 13; --size) {
        value[i]->SetFontSize(size);
        if (value[i]->GetTextWidth() <= room) break;
    }
    label[i]->SetPosition(textX, labelY);
    value[i]->SetPosition(textX, labelY + (s.primary ? 28 : 23));
}

void GuiTiles::Draw() {
    if (!IsVisible() || !tiles) return;
    const int alpha = GetAlpha();
    if (dirty) {
        dirty = false;
        for (int i = 0; i < Count(); ++i) Layout(i);
    }
    const int lit = hover >= 0 ? hover : (AnyPointer() ? -1 : focus);
    for (int i = 0; i < Count(); ++i) {
        const TileSpec& s = (*tiles)[i];
        float& l = lift[i];
        l += ((i == lit ? 1.0f : 0.0f) - l) * 0.3f;
        if (l < 0.01f) l = 0.0f;
        if (l > 0.99f) l = 1.0f;
        // Lit, the tile grows a little about its middle and its ring fades in.
        const float grow = 1.0f + kLiftScale * l;
        const float w = s.w * grow, h = s.h * grow;
        const float x = s.x - (w - s.w) / 2, y = s.y - (h - s.h) / 2;
        const skin::Tex& base = s.primary ? skin::tile9Primary : skin::tile9;
        const skin::Tex& over = s.primary ? skin::tile9PrimaryOver : skin::tile9Over;
        const int tileAlpha = s.dim ? alpha * 3 / 4 : alpha;
        if (l < 1.0f) skin::DrawNine(base, x - kShadow, y - kShadow, w + 2 * kShadow, h + 2 * kShadow, skin::kTileCorner, tileAlpha);
        if (l > 0.0f)
            skin::DrawNine(over, x - kShadow, y - kShadow, w + 2 * kShadow, h + 2 * kShadow, skin::kTileCorner,
                           static_cast<int>(tileAlpha * l));
        if (s.icon && s.icon->data) {
            const float iconX = s.x + kPad;
            const float iconY = Tall(s) ? s.y + kPad : s.y + (s.h - kIcon) / 2.0f;
            skin::Draw(*s.icon, iconX, iconY, s.dim ? alpha / 2 : alpha);
        }
        // The lit tile's name scrolls when it does not fit; the others end in "...".
        label[i]->SetScroll(i == lit ? SCROLL::HORIZONTAL : SCROLL::NONE);
        label[i]->Draw();
        if (!s.value.empty()) value[i]->Draw();
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
        soundClick->Play();
    }
}
