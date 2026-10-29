// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"
#include "riftwii/namekeys.hpp"

// The search box: a card with the text typed so far and an on-screen
// keyboard under it. Point and press A on a key, or move with the D-pad and
// press A. Minus deletes the last letter, Plus searches; B or HOME cancels. The widget
// covers the whole screen and dims what is behind it.
//
// The hex mode is the same card with 0-9 and A-F only, for a cheat's
// values: its own title, at most `maxLength` digits, and a note above the
// card (what the value is, from the cheat's notes).
//
// The name mode types a name (a Home tile's): letters in both cases, with
// a Shift key for the next character, on by itself at the start, after a
// space and after '(' or '[' (capitals only, as a phone's keyboard), and pressed by the player
// for capitals and the symbols on the number row and the bottom row's last
// three keys (riftwii/namekeys.hpp); OK in place of Search.
//
// A USB keyboard types too (usbinput.hpp): its letters, digits and the
// characters the card has, Enter as Search (OK), Backspace as Del and Esc
// as Cancel. A name keeps the case typed.
class GuiSearchKeys : public GuiElement {
public:
    static constexpr std::size_t kMaxLength = 32;

    explicit GuiSearchKeys(const std::string& start);
    GuiSearchKeys(const std::string& start, const std::string& title, const std::string& note, std::size_t maxLength);
    // The name mode.
    GuiSearchKeys(const std::string& start, const std::string& title, std::size_t maxLength);
    ~GuiSearchKeys() override;
    const std::string& Text() const { return text; }
    // A short line at the right of the title (Home's live search: how many
    // games match what is typed so far). Called with the GUI halted.
    void SetCount(const std::string& line);
    // The games that match so far, on a box above the card (the keyboard
    // hides the grid behind it); empty hides the box. GUI halted.
    void SetMatches(const std::string& text);
    // Home's search: the characters that still lead to a game when typed
    // next (lower case; ' ' for Space). The other letter keys are dimmed,
    // can't be typed and the D-pad passes over them, so a name is a few
    // presses away. Not called (the hex pad): every key. GUI halted.
    void SetAvailable(const std::string& chars);
    // Search was chosen (1), cancelled (-1), or neither yet (0).
    int Result() const { return result; }
    void Draw() override;
    void Update(GuiTrigger* t) override;

private:
    struct Key {
        std::string label;
        char ch;  // typed; 0 for the action keys
        enum class Act { Type, Space, Back, Clear, Cancel, Search, Shift } act;
        int row, x, w;
        GuiText* caption;
        int captionSize = 18;
    };
    void Press(const Key& k);
    // What a USB keyboard typed since the last frame.
    void TypeFromKeyboard();
    // Whether `k` can be pressed: an action, or a character still available.
    bool Live(const Key& k) const;
    int KeyAt(int x, int y) const;
    int Neighbour(int from, int dRow) const;
    int Y(int row) const;

    enum class Mode { Search, Hex, Name };
    void Build(const std::string& titleText, const std::string& noteText, Mode mode);
    // The name mode: the letter keys show the case the next letter takes,
    // and the symbol keys their symbol while Shift is on.
    void ShowCase();

    std::vector<Key> keys;
    std::string text;
    std::string available;  // SetAvailable's characters
    bool limited = false;   // SetAvailable was called
    std::size_t maxLength = kMaxLength;
    bool hex = false;
    bool naming = false;  // the name mode
    riftwii::NameShift shift = riftwii::NameShift::Off;  // the name mode: what the next key types
    GuiText* shown;
    GuiText* title;
    GuiText* note = nullptr;
    GuiText* count = nullptr;
    GuiText* matches = nullptr;
    bool showMatches = false;
    GuiSound* soundOver;
    GuiSound* soundClick;
    int focus = 0;
    int hover = -1;
    int result = 0;
    float blink = 0;  // elapsed milliseconds in the cursor blink cycle
};
