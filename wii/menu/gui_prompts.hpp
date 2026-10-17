// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"

// The prompts along the bottom of a screen, as consoles show them: each
// button's name on a round badge with what it does there ("A  Select",
// "B  Back", "+  Play"), from the right edge of the safe area inwards, so
// no one has to remember what a button does on which screen. The pointer
// can press them too: a prompt it is on lights up, and A on it counts as
// that button.
class GuiPrompts : public GuiElement {
public:
    GuiPrompts();
    ~GuiPrompts() override;
    // `button`: the button's name on the badge ("A", "B", "+", "1", "2").
    // Returns the prompt's index.
    int Add(const std::string& button, const std::string& action);
    void SetAction(int index, const std::string& action);
    void SetShown(int index, bool shown);
    // The prompt the pointer pressed since the last call; -1 when none.
    int GetClicked();
    void Draw() override;
    void Update(GuiTrigger* t) override;

    // Where the shown prompts' cards begin on the screen: the left edge of
    // the leftmost one, or the right edge of the area when none is shown.
    // For text that shares the bottom of the screen with them.
    int LeftEdge();
    // The y the cards begin at (their soft shadow reaches a little above).
    static int CardTop();

    // The prompts' line: inside a TV's overscan (5% of 480 from the bottom).
    static constexpr int kY = 422;  // the buttons end at y 450, inside the overscan (456)

private:
    struct Prompt {
        GuiText* badge;
        GuiText* text;
        bool shown = true;
        bool symbol = false;  // + or -, not a letter
        std::string button;   // its name, for its picture (skin::ButtonIcon)
        int x = 0, w = 0;  // where it is drawn, worked out by Draw
    };
    std::vector<Prompt> prompts;
    void DrawBadgeText(Prompt& p);
    int hover = -1, clicked = -1;
    GuiSound* soundOver;
    GuiSound* soundClick;
};
