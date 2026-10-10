// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_searchkeys.hpp"

#include "i18n.hpp"
#include "riftwii/skinpaint.hpp"
#include "skin.hpp"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;
using riftwii::wii::tr;

namespace {

constexpr int kPanelX = 34, kPanelY = 102;
constexpr int kKeyLeft = 60, kKeyPitch = 52, kKeyW = 48, kKeyH = 30;
constexpr int kFirstRowY = 190, kRowPitch = 35, kActionRowY = 334, kActionH = 34;
constexpr int kRows = 5;  // four of letters, one of actions

bool Pressed(GuiTrigger* t, u32 wpad, u32 drc) {
    return (t->wpad && (t->wpad->btns_d & wpad)) || (t->wiidrcdata.btns_d & drc);
}

bool PressedA(GuiTrigger* t) {
    return Pressed(t, WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, WIIDRC_BUTTON_A) || (t->pad.btns_d & PAD_BUTTON_A);
}

bool AnyPointer() {
    for (int i = 0; i < 4; i++)
        if (userInput[i].wpad && userInput[i].wpad->ir.valid) return true;
    return false;
}

}  // namespace

int GuiSearchKeys::Y(int row) const { return row == kRows - 1 ? kActionRowY : kFirstRowY + row * kRowPitch; }

GuiSearchKeys::GuiSearchKeys(const std::string& start) : text(start.substr(0, kMaxLength)) {
    Build(tr("Search games"), "", false);
}

GuiSearchKeys::GuiSearchKeys(const std::string& start, const std::string& titleText, const std::string& noteText,
                             std::size_t maxLength)
    : text(start.substr(0, maxLength)), maxLength(maxLength) {
    Build(titleText, noteText, true);
}

void GuiSearchKeys::Build(const std::string& titleText, const std::string& noteText, bool hexKeys) {
    hex = hexKeys;
    width = screenwidth;
    height = screenheight;
    selectable = true;
    const int wide = 4 * kKeyPitch - 4, mid = 2 * kKeyPitch - 4;
    if (hex) {
        // Two rows of digits; the action row two rows down, as the letters'.
        const char* const rows[2] = {"0123456789", "ABCDEF"};
        for (int r = 0; r < 2; ++r) {
            int col = 0;
            for (const char* p = rows[r]; *p; ++p, ++col)
                keys.push_back({std::string(1, *p), *p, Key::Act::Type, r, kKeyLeft + col * kKeyPitch, kKeyW, nullptr});
        }
        keys.push_back({tr("Del"), 0, Key::Act::Back, 1, kKeyLeft + 9 * kKeyPitch, kKeyW, nullptr});
        keys.push_back({tr("Clear"), 0, Key::Act::Clear, 4, kKeyLeft + 2 * kKeyPitch, mid, nullptr});
        keys.push_back({tr("Cancel"), 0, Key::Act::Cancel, 4, kKeyLeft + 4 * kKeyPitch, mid, nullptr});
        keys.push_back({tr("OK"), 0, Key::Act::Search, 4, kKeyLeft + 6 * kKeyPitch, wide, nullptr});
    } else {
        const char* const rows[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM-'."};
        for (int r = 0; r < 4; ++r) {
            int col = 0;
            for (const char* p = rows[r]; *p; ++p, ++col)
                keys.push_back({std::string(1, *p), *p, Key::Act::Type, r, kKeyLeft + col * kKeyPitch, kKeyW, nullptr});
        }
        // The third row is one short: the delete key takes the last place.
        keys.push_back({tr("Del"), 0, Key::Act::Back, 2, kKeyLeft + 9 * kKeyPitch, kKeyW, nullptr});
        // Actions: Space, Clear, Cancel and Search across the card's width.
        keys.push_back({tr("Space"), ' ', Key::Act::Space, 4, kKeyLeft, wide, nullptr});
        keys.push_back({tr("Clear"), 0, Key::Act::Clear, 4, kKeyLeft + 4 * kKeyPitch, mid, nullptr});
        keys.push_back({tr("Cancel"), 0, Key::Act::Cancel, 4, kKeyLeft + 6 * kKeyPitch, mid, nullptr});
        keys.push_back({tr("Search"), 0, Key::Act::Search, 4, kKeyLeft + 8 * kKeyPitch, mid, nullptr});
    }
    for (Key& k : keys) {
        k.caption = new GuiText(k.label.c_str(), 18, skin::kInk);
        k.caption->SetParent(this);
        k.caption->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    }
    // Search (or OK) when there is text (A straight away), else Q, the
    // first letter (0 in hex).
    focus = text.empty() ? (hex ? 0 : 10) : static_cast<int>(keys.size()) - 1;
    if (!noteText.empty()) {
        note = new GuiText(noteText.c_str(), 16, skin::kInk);
        note->SetParent(this);
        note->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
        note->SetPosition(kPanelX + 10, 20);
        note->SetWrap(true, 572 - 20, 3);
    }
    title = new GuiText(titleText.c_str(), 24, skin::kInk);
    title->SetParent(this);
    title->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    title->SetPosition(56, 112);  // clear of the text field at y 148, descenders included
    count = new GuiText("", 18, skin::kInkSoft);
    count->SetParent(this);
    count->SetAlignment(ALIGN_H::RIGHT, ALIGN_V::TOP);
    count->SetPosition(-(screenwidth - (kKeyLeft + 10 * kKeyPitch - 4)), 118);
    shown = new GuiText(text.c_str(), 22, skin::kInk);
    shown->SetParent(this);
    shown->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    shown->SetPosition(kKeyLeft + 10, 154);
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiSearchKeys::~GuiSearchKeys() {
    for (Key& k : keys) delete k.caption;
    delete note;
    delete title;
    delete count;
    delete shown;
    delete soundOver;
    delete soundClick;
}

void GuiSearchKeys::SetCount(const std::string& line) { count->SetText(line.c_str()); }

int GuiSearchKeys::KeyAt(int x, int y) const {
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const Key& k = keys[i];
        const int ky = Y(k.row), kh = k.row == kRows - 1 ? kActionH : kKeyH;
        if (x >= k.x && x < k.x + k.w && y >= ky && y < ky + kh) return static_cast<int>(i);
    }
    return -1;
}

// The key in the row above or below whose middle is nearest this one's.
int GuiSearchKeys::Neighbour(int from, int dRow) const {
    const Key& f = keys[static_cast<std::size_t>(from)];
    int row = f.row + dRow;
    // Rows with no keys (the hex keypad's) are passed over.
    const auto empty = [&](int r) {
        for (const Key& k : keys)
            if (k.row == r) return false;
        return true;
    };
    while (row >= 0 && row < kRows && empty(row)) row += dRow;
    if (row < 0 || row >= kRows) return from;
    int best = from, gap = 1 << 30;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].row != row) continue;
        const int d = std::abs((keys[i].x + keys[i].w / 2) - (f.x + f.w / 2));
        if (d < gap) {
            gap = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void GuiSearchKeys::Press(const Key& k) {
    switch (k.act) {
    case Key::Act::Type:
    case Key::Act::Space:
        if (text.size() < maxLength) text += k.ch;
        break;
    case Key::Act::Back:
        if (!text.empty()) text.pop_back();
        break;
    case Key::Act::Clear:
        text.clear();
        break;
    case Key::Act::Cancel:
        result = -1;
        break;
    case Key::Act::Search:
        if (hex && text.empty()) break;  // a value needs a digit
        result = 1;
        break;
    }
    shown->SetText(text.c_str());
}

void GuiSearchKeys::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || result != 0) return;
    if (Pressed(t, WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B | WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME,
                WIIDRC_BUTTON_B | WIIDRC_BUTTON_HOME) ||
        (t->pad.btns_d & PAD_BUTTON_B)) {
        result = -1;
        return;
    }
    // Plus (Start on a GameCube controller) as the Search or OK key.
    if (Pressed(t, WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, WIIDRC_BUTTON_PLUS) || (t->pad.btns_d & PAD_BUTTON_START)) {
        for (const Key& k : keys)
            if (k.act == Key::Act::Search) Press(k);
        return;
    }
    if (Pressed(t, WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, WIIDRC_BUTTON_MINUS)) {
        if (!text.empty()) text.pop_back();
        shown->SetText(text.c_str());
        return;
    }
    if (t->wpad && t->wpad->ir.valid) {
        const int x = static_cast<int>(t->wpad->ir.x), y = static_cast<int>(t->wpad->ir.y);
        const int at = KeyAt(x, y);
        if (at != hover && at >= 0) soundOver->Play();
        hover = at;
        if (at >= 0) focus = at;
        if (at >= 0 && PressedA(t)) {
            soundClick->Play();
            Press(keys[static_cast<std::size_t>(at)]);
        }
        return;
    }
    if (AnyPointer()) return;  // another controller points; it decides
    hover = -1;
    int to = focus;
    const Key& f = keys[static_cast<std::size_t>(focus)];
    if (t->Up()) {
        to = Neighbour(focus, -1);
    } else if (t->Down()) {
        to = Neighbour(focus, 1);
    } else if (t->Left()) {
        // Same row, the key just left of this one.
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i].row == f.row && keys[i].x < f.x && (to == focus || keys[i].x > keys[static_cast<std::size_t>(to)].x))
                to = static_cast<int>(i);
    } else if (t->Right()) {
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i].row == f.row && keys[i].x > f.x && (to == focus || keys[i].x < keys[static_cast<std::size_t>(to)].x))
                to = static_cast<int>(i);
    }
    if (to != focus) {
        focus = to;
        soundOver->Play();
    }
    if (PressedA(t)) {
        soundClick->Play();
        Press(keys[static_cast<std::size_t>(focus)]);
    }
}

void GuiSearchKeys::Draw() {
    Menu_FillWholeScreen((GXColor){0, 0, 0, 150});
    if (note) {
        // On a box of its own: what is behind is dimmed, any colour.
        Menu_DrawRectangle(kPanelX, 12, 572, 76, skin::kBadge, 1);
        note->Draw();
    }
    skin::Draw(skin::panelSettings, kPanelX - 4, kPanelY - 4);
    title->Draw();
    count->Draw();
    // The text field, with a caret that blinks. Every colour is the theme's
    // (a dark one has light ink), as in the rest of the menu.
    const int fx = kKeyLeft, fy = 148, fw = 10 * kKeyPitch - 4, fh = 34;
    Menu_DrawRectangle(fx, fy, fw, fh, skin::kBadge, 1);
    Menu_DrawRectangle(fx, fy + fh - 2, fw, 2, skin::kAccent, 1);
    shown->Draw();
    blink = (blink + 1) % 60;
    if (blink < 35) {
        const int cx = kKeyLeft + 10 + (text.empty() ? 0 : shown->GetTextWidth()) + 2;
        Menu_DrawRectangle(cx, fy + 6, 2, 22, skin::kInk, 1);
    }
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const Key& k = keys[i];
        const bool on = static_cast<int>(i) == focus && (hover >= 0 || !AnyPointer());
        const int ky = Y(k.row), kh = k.row == kRows - 1 ? kActionH : kKeyH;
        // Keys as the Wii's: shaded and glossy; Search filled with the
        // accent; a lit key ringed in the accent, with its glow.
        const bool primary = k.act == Key::Act::Search;
        skin::DrawNine(skin::keys[(primary ? 2 : 0) + (on ? 1 : 0)], k.x - 4, ky - 4, k.w + 8, kh + 8, riftwii::kKeyCorner);
        k.caption->SetColor(primary ? skin::kTextOnAccent : skin::kInk);
        k.caption->SetPosition(k.x + (k.w - k.caption->GetTextWidth()) / 2, ky + (kh - 18) / 2 - 1);
        k.caption->Draw();
    }
}
