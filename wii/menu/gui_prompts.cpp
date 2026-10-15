// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_prompts.hpp"

#include "skin.hpp"
#include "video.h"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kRight = 600;  // 8 inside the safe area's right edge (5% of 640 in from the side)
constexpr int kBadge = 24;   // skin::promptButton
constexpr int kGap = 26;     // between two prompts
constexpr int kTextGap = 8;  // between a badge and its text

}  // namespace

GuiPrompts::GuiPrompts() {
    width = screenwidth;
    height = screenheight;
    soundOver = new GuiSound(button_over_pcm, button_over_pcm_size, SOUND::PCM);
    soundClick = new GuiSound(button_click_pcm, button_click_pcm_size, SOUND::PCM);
}

GuiPrompts::~GuiPrompts() {
    for (Prompt& p : prompts) {
        delete p.badge;
        delete p.text;
    }
    delete soundOver;
    delete soundClick;
}

int GuiPrompts::Add(const std::string& button, const std::string& action) {
    Prompt p;
    // In the card's colour on the glyph's: two-tone in every theme (a dark one
    // has a light glyph).
    p.badge = new GuiText(button.c_str(), 14, skin::kCard);
    p.badge->SetParent(this);
    // Centred on the badge by the font itself (its own advance and
    // height), which a width halved by hand put a pixel or two off.
    p.badge->SetAlignment(ALIGN_H::CENTRE, ALIGN_V::MIDDLE);
    p.text = new GuiText(action.c_str(), 16, skin::kInkSoft);
    p.text->SetParent(this);
    p.text->SetAlignment(ALIGN_H::LEFT, ALIGN_V::TOP);
    p.symbol = button == "+" || button == "-";
    prompts.push_back(p);
    return static_cast<int>(prompts.size()) - 1;
}

void GuiPrompts::SetAction(int index, const std::string& action) {
    if (index >= 0 && index < static_cast<int>(prompts.size())) prompts[index].text->SetText(action.c_str());
}

void GuiPrompts::SetShown(int index, bool shown) {
    if (index >= 0 && index < static_cast<int>(prompts.size())) prompts[index].shown = shown;
}

int GuiPrompts::GetClicked() {
    const int c = clicked;
    clicked = -1;
    return c;
}

void GuiPrompts::Draw() {
    if (!IsVisible()) return;
    const int alpha = GetAlpha();
    // Laid out from the right: the first prompt added is the rightmost.
    int right = kRight;
    for (int i = 0; i < static_cast<int>(prompts.size()); ++i) {
        Prompt& p = prompts[i];
        if (!p.shown) continue;
        const int textW = p.text->GetTextWidth();
        p.w = kBadge + kTextGap + textW;
        p.x = right - p.w;
        right = p.x - kGap;
        const bool lit = i == hover;
        skin::Draw(skin::promptButton, static_cast<float>(p.x), static_cast<float>(kY), alpha);
        // Offsets from the middle of this element, which covers the screen.
        // The font's middle takes in its descenders, so a capital sits low:
        // one up; a + or a - (drawn at a capital's middle) one more, and one
        // to the left, where its advance leaves it.
        const bool sign = p.symbol;
        p.badge->SetPosition(p.x + kBadge / 2 - GetWidth() / 2 - (sign ? 1 : 0),
                             kY + kBadge / 2 - GetHeight() / 2 - (sign ? 2 : 1));
        p.badge->Draw();
        p.text->SetColor(lit ? skin::kAccentInk : skin::kInkSoft);
        p.text->SetPosition(p.x + kBadge + kTextGap, kY + 2);
        p.text->Draw();
    }
    UpdateEffects();
}

void GuiPrompts::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t || !t->wpad || !t->wpad->ir.valid) return;
    const int px = static_cast<int>(t->wpad->ir.x), py = static_cast<int>(t->wpad->ir.y);
    int at = -1;
    for (int i = 0; i < static_cast<int>(prompts.size()); ++i) {
        const Prompt& p = prompts[i];
        if (p.shown && p.w > 0 && px >= p.x - 6 && px < p.x + p.w + 6 && py >= kY - 8 && py < kY + kBadge + 8) at = i;
    }
    if (at != hover && at >= 0) soundOver->Play();
    hover = at;
    if (at >= 0 && (t->wpad->btns_d & (WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A))) {
        clicked = at;
        soundClick->Play();
    }
}
