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

    // The prompts' line: inside a TV's overscan (5% of 480 from the bottom).
    static constexpr int kY = 426;  // the badges end at y 450, inside the overscan (456)

private:
    struct Prompt {
        GuiText* badge;
        GuiText* text;
        bool shown = true;
        bool symbol = false;  // + or -, not a letter
        int x = 0, w = 0;  // where it is drawn, worked out by Draw
    };
    std::vector<Prompt> prompts;
    int hover = -1, clicked = -1;
    GuiSound* soundOver;
    GuiSound* soundClick;
};
