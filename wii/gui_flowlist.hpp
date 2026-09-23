// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"

// The game page's list of mod packs and their options, and the settings
// list: rows in a white panel, each with a chip on the right showing its
// value. A (pointer or D-pad focus) acts on a row; - steps an option back.
struct FlowRow {
    enum class Kind { Header, Option, Action, Info };
    Kind kind = Kind::Info;
    std::string label;
    std::string value;  // the chip's text; empty for Info rows
    bool on = false;    // accent chip
    bool dim = false;   // greyed label (broken pack, notes)
};

class GuiFlowList : public GuiElement {
public:
    static constexpr int kRowHeight = 34;
    // At (x, y) on screen, `rowWidth` wide, `visibleRows` tall.
    GuiFlowList(int x, int y, int rowWidth, int visibleRows);
    ~GuiFlowList() override;
    void SetRows(const std::vector<FlowRow>* rows);  // must outlive the list
    void Refresh();                                  // after the rows changed
    void Select(int index);
    int Selected() const { return focus; }
    // The row A was pressed on, or - (back) was pressed on, since the last call; -1 when none.
    int GetClicked();
    int GetClickedBack();
    void Draw() override;
    void Update(GuiTrigger* t) override;

private:
    static constexpr int kMaxVisible = 8;
    int Count() const { return rows ? static_cast<int>(rows->size()) : 0; }
    void ScrollTo(int first);
    int RowAt(int x, int y) const;

    const std::vector<FlowRow>* rows = nullptr;
    int x0, y0, rowWidth, visible;
    int offset = 0, focus = 0, hover = -1;
    int clicked = -1, clickedBack = -1;
    bool dirty = true;
    GuiText* label[kMaxVisible];
    GuiText* value[kMaxVisible];
    GuiSound* soundOver;
    GuiSound* soundClick;
};
