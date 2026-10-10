// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"

// The search box: a card with the text typed so far and an on-screen
// keyboard under it. Point and press A on a key, or move with the D-pad and
// press A. Minus deletes the last letter, Plus searches; B or HOME cancels. The widget
// covers the whole screen and dims what is behind it.
//
// The hex mode is the same card with 0-9 and A-F only, for a cheat's
// values: its own title, at most `maxLength` digits, and a note above the
// card (what the value is, from the cheat's notes).
class GuiSearchKeys : public GuiElement {
public:
    static constexpr std::size_t kMaxLength = 32;

    explicit GuiSearchKeys(const std::string& start);
    GuiSearchKeys(const std::string& start, const std::string& title, const std::string& note, std::size_t maxLength);
    ~GuiSearchKeys() override;
    const std::string& Text() const { return text; }
    // A short line at the right of the title (Home's live search: how many
    // games match what is typed so far). Called with the GUI halted.
    void SetCount(const std::string& line);
    // Search was chosen (1), cancelled (-1), or neither yet (0).
    int Result() const { return result; }
    void Draw() override;
    void Update(GuiTrigger* t) override;

private:
    struct Key {
        std::string label;
        char ch;  // typed; 0 for the action keys
        enum class Act { Type, Space, Back, Clear, Cancel, Search } act;
        int row, x, w;
        GuiText* caption;
    };
    void Press(const Key& k);
    int KeyAt(int x, int y) const;
    int Neighbour(int from, int dRow) const;
    int Y(int row) const;

    void Build(const std::string& titleText, const std::string& noteText, bool hex);

    std::vector<Key> keys;
    std::string text;
    std::size_t maxLength = kMaxLength;
    bool hex = false;
    GuiText* shown;
    GuiText* title;
    GuiText* note = nullptr;
    GuiText* count = nullptr;
    GuiSound* soundOver;
    GuiSound* soundClick;
    int focus = 0;
    int hover = -1;
    int result = 0;
    int blink = 0;
};
