// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui_prompts.hpp"

#include "drawtrace.hpp"
#include "skin.hpp"
#include "transition.hpp"
#include "video.h"
#include "wiidrc.h"

namespace skin = riftwii::wii::skin;

namespace {

constexpr int kBadge = 24;   // skin::promptButton
constexpr int kGap = 10;     // between two prompts' buttons
constexpr int kTextGap = 8;  // between a badge and its text
// The room each side of a badge and its word (the cards they once sat on
// kept this spacing, which the screens' layouts are measured against).
constexpr int kPadLeft = 9, kPadRight = 9, kPadY = 4;

// A button's press dips its prompt. This observes input only; each screen
// handles its own buttons.
bool PromptPressed(const std::string& name, const GuiTrigger& trigger) {
    struct Key {
        const char* name;
        u32 remote;
        u16 pad, gamepad;
    };
    static const Key keys[] = {
        {"A", WPAD_BUTTON_A | WPAD_CLASSIC_BUTTON_A, PAD_BUTTON_A, WIIDRC_BUTTON_A},
        {"B", WPAD_BUTTON_B | WPAD_CLASSIC_BUTTON_B, PAD_BUTTON_B, WIIDRC_BUTTON_B},
        {"+", WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS, PAD_BUTTON_START, WIIDRC_BUTTON_PLUS},
        {"-", WPAD_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_MINUS, PAD_TRIGGER_L, WIIDRC_BUTTON_MINUS},
        {"1", WPAD_BUTTON_1 | WPAD_CLASSIC_BUTTON_Y, PAD_BUTTON_Y, WIIDRC_BUTTON_X},
        {"2", WPAD_BUTTON_2 | WPAD_CLASSIC_BUTTON_X, PAD_TRIGGER_Z, WIIDRC_BUTTON_Y},
    };
    for (const Key& key : keys) {
        if (name != key.name) continue;
        return (trigger.wpad && (trigger.wpad->btns_d & key.remote)) ||
               (trigger.pad.btns_d & key.pad) || (trigger.wiidrcdata.btns_d & key.gamepad);
    }
    return false;
}

}  // namespace

GuiPrompts::GuiPrompts() {
    width = screenwidth;
    height = screenheight;
}

GuiPrompts::~GuiPrompts() {
    for (Prompt& p : prompts) {
        delete p.badge;
        delete p.text;
    }
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
    p.button = button;
    prompts.push_back(p);
    return static_cast<int>(prompts.size()) - 1;
}

void GuiPrompts::SetAction(int index, const std::string& action) {
    if (index >= 0 && index < static_cast<int>(prompts.size())) prompts[index].text->SetText(action.c_str());
}

void GuiPrompts::SetShown(int index, bool shown) {
    if (index >= 0 && index < static_cast<int>(prompts.size())) prompts[index].shown = shown;
}

// The theme's badge's letter, for a button without a picture of its own.
void GuiPrompts::DrawBadgeText(Prompt& p) {
    // Offsets from the middle of this element, which covers the screen.
    // The font's middle takes in its descenders, so a capital sits low:
    // one up; a + or a - (drawn at a capital's middle) one more, and one
    // to the left, where its advance leaves it.
    const bool sign = p.symbol;
    p.badge->SetPosition(p.x + kBadge / 2 - GetWidth() / 2 - (sign ? 1 : 0),
                         kY + kBadge / 2 - GetHeight() / 2 - (sign ? 2 : 1));
    p.badge->Draw();
}

int GuiPrompts::CardTop() {
    return kY - kPadY;
}

int GuiPrompts::LeftEdge() {
    // The same walk from the right as Draw makes.
    int right = rightEdge;
    int left = rightEdge + kPadRight;
    for (Prompt& p : prompts) {
        if (!p.shown) continue;
        const int w = kBadge + kTextGap + p.text->GetTextWidth();
        const int x = right - kPadRight - w;
        left = x - kPadLeft;
        right = left - kGap;
    }
    return left;
}

void GuiPrompts::Draw() {
    if (!IsVisible()) return;
    riftwii::wii::DrawTrace trace("the prompts");
    const int alpha = GetAlpha();
    // Laid out from the right: the first prompt added is the rightmost.
    int right = rightEdge;
    for (int i = 0; i < static_cast<int>(prompts.size()); ++i) {
        Prompt& p = prompts[i];
        if (!p.shown) continue;
        const int textW = p.text->GetTextWidth();
        p.w = kBadge + kTextGap + textW;
        p.x = right - kPadRight - p.w;
        right = p.x - kPadLeft - kGap;
        const float pressScale = riftwii::wii::transition::PressScale(p.pressElapsed);
        Menu_PushCamera(pressScale, p.x + p.w / 2.0f, kY + kBadge / 2.0f);
        // The button's own picture (Kenney's Input Prompts); the theme's
        // badge with its name for one that has none.
        const skin::Tex& picture = skin::ButtonIcon(p.button, kBadge);
        if (picture.data) {
            skin::Draw(picture, static_cast<float>(p.x), static_cast<float>(kY), alpha);
        } else {
            skin::Draw(skin::promptButton, static_cast<float>(p.x), static_cast<float>(kY), alpha);
            DrawBadgeText(p);
        }
        p.text->SetColor(skin::kInkSoft);
        // One down from the top puts the word's capitals on the button's
        // middle (measured on a screenshot), with the badge's letter.
        p.text->SetPosition(p.x + kBadge + kTextGap, kY + 1);
        p.text->Draw();
        Menu_PopCamera();
    }
    UpdateEffects();
}

void GuiPrompts::Update(GuiTrigger* t) {
    if (state == STATE::DISABLED || !t) return;
    // While the Remote points, A presses whatever is under the pointer, not
    // what the A prompt names, so A's prompt dips only without a pointer.
    const bool pointing = t->wpad && t->wpad->ir.valid;
    for (Prompt& prompt : prompts) {
        const bool pointerOwnsA = pointing && prompt.button == "A";
        if (prompt.shown && !pointerOwnsA && PromptPressed(prompt.button, *t)) prompt.pressElapsed = 0;
    }
}
