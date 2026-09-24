// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"

// The lists of the game page, its Mods and Cheats pages and Settings: rows
// in a white panel, each with its control on the right.
//   Option  a value between two arrow buttons: the right one (or the row)
//           steps it on, the left one (or -) steps it back
//   Toggle  an On/Off switch
//   Action  a chip that opens a page or does something, with a ">"
//   Header  a fixed chip (a broken pack's "Broken")
//   Info    plain text
// With the pointer, holding A and moving the Wii Remote drags the list; a
// press that does not move acts on the row when A is let go. The D-pad
// moves the focus, and Left/Right step an option.
struct FlowRow {
    enum class Kind { Header, Option, Toggle, Action, Info };
    Kind kind = Kind::Info;
    std::string label;
    std::string value;     // the control's text (Toggle: "On"/"Off" beside the switch)
    bool on = false;       // accent colour; a Toggle's state
    bool dim = false;      // greyed and fixed (broken pack, notes, unavailable)
    bool heading = false;  // larger label (a pack)
    bool indent = false;   // under a heading (a pack's options)
};

class GuiFlowList : public GuiElement {
public:
    static constexpr int kRowHeight = 44;
    // At (x, y) on screen, `rowWidth` wide, `visibleRows` tall.
    GuiFlowList(int x, int y, int rowWidth, int visibleRows);
    ~GuiFlowList() override;
    void SetRows(const std::vector<FlowRow>* rows);  // must outlive the list
    void Refresh();                                  // after the rows changed
    void Select(int index);
    int Selected() const { return focus; }
    // The row acted on (A, or stepped forward), or stepped back (-, the
    // left arrow), since the last call; -1 when none.
    int GetClicked();
    int GetClickedBack();
    void Draw() override;
    void Update(GuiTrigger* t) override;

private:
    enum class Part { None, Body, Back, Forward };
    static constexpr int kMaxVisible = 8;
    int Count() const { return rows ? static_cast<int>(rows->size()) : 0; }
    int MaxScroll() const;
    void ScrollTo(float y);
    int RowAt(int x, int y) const;
    Part PartAt(int row, int x) const;
    bool Actionable(int row) const;
    bool OnTrack(int x, int y) const;
    void ScrollFromTrack(int y);

    const std::vector<FlowRow>* rows = nullptr;
    int x0, y0, rowWidth, visible;
    float scroll = 0, aim = 0, fling = 0;  // pixels from the top
    int focus = 0, hover = -1;
    Part hoverPart = Part::None;
    int clicked = -1, clickedBack = -1;
    // A held on the list: which Wii Remote, where it started, what it was on.
    int grabChan = -1, grabY = 0, grabRow = -1;
    float grabScroll = 0;
    Part grabPart = Part::None;
    bool dragging = false, grabTrack = false;
    int textFirst = -1;
    bool dirty = true;
    GuiText* label[kMaxVisible + 1];
    GuiText* value[kMaxVisible + 1];
    GuiSound* soundOver;
    GuiSound* soundClick;
};
