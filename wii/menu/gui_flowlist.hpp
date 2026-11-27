// SPDX-FileCopyrightText: 2026 RiftWii contributors
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
// moves the focus, and Left/Right step an option. A longer list has up and
// down arrows on its right edge, as the Wii Menu's lists do: pointing at
// one and pressing (or holding) A scrolls it, and each goes away at its end.
struct FlowRow {
    enum class Kind { Header, Option, Toggle, Action, Info };
    Kind kind = Kind::Info;
    std::string label;
    std::string value;     // the control's text (Toggle: "On"/"Off" beside the switch)
    std::string detail;    // a second line under the label (a backup's "2 saves · 4 blocks"), in a list made taller with SetRowHeight; empty for none
    bool on = false;       // accent colour; a Toggle's state
    bool dim = false;      // greyed and fixed (broken pack, notes, unavailable)
    bool heading = false;  // larger label (a pack)
    bool indent = false;   // under a heading (a pack's options)
    // An Action with no value that opens another screen, a choice or the
    // keyboard: a ">" at its end, as a tile that opens a page has. One
    // that acts at once (Hide from Home) has none, so the two look different.
    bool opens = false;
    // A picture at the row's start (a memory card save's icon): room is kept
    // for it when `icon_column` is set, and `icon` (32x32 GX RGB5A3 tiles,
    // 32-byte aligned, kept by the caller while the list shows it) is drawn
    // there when it is not null. A save with no icon keeps its place blank.
    bool icon_column = false;
    const unsigned char* icon = nullptr;
    // Settings (SetNotesUnder): when a change takes effect ("Next game"),
    // at the right of the row's second line; empty when it is at once.
    std::string tag;
    // An Action that does something rather than open or show a setting
    // (Test, Rescan): on cards its value is drawn as a button of its own.
    bool button = false;
};

class GuiFlowList : public GuiElement {
public:
    static constexpr int kRowHeight = 44;
    // At (x, y) on screen, `rowWidth` wide (the scroll arrows included,
    // when it scrolls), `visibleRows` tall.
    GuiFlowList(int x, int y, int rowWidth, int visibleRows);
    ~GuiFlowList() override;
    void SetRows(const std::vector<FlowRow>* rows);  // must outlive the list
    // The rows on the backdrop with no panel under them. What can be
    // pressed looks it: each such row on a card (the lit one with the
    // tiles' ring), an option's value between its arrows, an action's
    // ending in ">". What is only read (an Info row: a note, or a fact
    // with a value) is plain text, never lit.
    void SetCards(bool on) { cards = on; }
    // Two lists side by side (the Miis page): only the active one answers
    // the D-pad and A, and shows its lit row without the pointer. The
    // pointer works on both. On by default.
    void SetKeysActive(bool on) { keysActive = on; }
    void Refresh();                                  // after the rows changed
    void Select(int index);
    int Selected() const { return focus; }
    // Where row `index` is on screen now (its top), scrolled.
    int RowTop(int index) const { return y0 + index * rowH - static_cast<int>(scroll); }
    // Taller rows for a list whose rows have a second line (FlowRow::detail);
    // kRowHeight by default. Set before the list is shown.
    void SetRowHeight(int h) { rowH = h; }
    // Settings' rows: the label and its control share the row's top line
    // and the detail (a short explanation) runs the full width under both,
    // so a translation has room. Set with SetRowHeight, before it shows.
    void SetNotesUnder(bool on) { notesUnder = on; }
    // Scrolled to the bottom (a list that fits is always there).
    bool AtEnd() const { return scroll >= static_cast<float>(MaxScroll()) - 1.0f; }
    // The row acted on (A, or stepped forward), or stepped back (-, the
    // left arrow), since the last call; -1 when none.
    int GetClicked();
    int GetClickedBack();
    // Whether the last GetClicked() row was stepped on (its right arrow, or
    // Right on the D-pad) rather than pressed with A: Settings opens a
    // choice's list on A and steps it on the arrows.
    bool LastClickStepped() const { return clickStepped; }
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
    // -1 on the up arrow, 1 on the down arrow (while shown), else 0.
    int ArrowAt(int x, int y) const;
    int ArrowX() const;
    int TrackX() const;
    int RowsRight() const;
    int ControlsRight() const;
    int ArrowY(int dir) const;
    void ScrollFromTrack(int y);

    const std::vector<FlowRow>* rows = nullptr;
    int x0, y0, rowWidth, visible;
    float scroll = 0, aim = 0, fling = 0;  // pixels from the top
    int litRow = -1;  // keep the last card while its highlight fades out
    float litPos = 0, litFade = 0;          // the highlight: at which row (eased), how shown
    std::vector<float> switchPos;           // each switch, 0 off to 1 on (eased; -1 not drawn yet)
    std::vector<std::string> shownValue;    // each row's value as last drawn
    std::vector<float> valueIn;             // and how far a new one has come in
    int focus = 0, hover = -1;
    Part hoverPart = Part::None;
    int clicked = -1, clickedBack = -1;
    bool clickStepped = false;
    bool notesUnder = false;
    // A held on the list: which Wii Remote, where it started, what it was on.
    int grabChan = -1, grabY = 0, grabRow = -1;
    float grabScroll = 0;
    Part grabPart = Part::None;
    bool dragging = false, grabTrack = false;
    int grabArrow = 0, arrowHeld = 0;  // an arrow held: which, and for how many frames
    float arrowLift[2] = {};
    int hoverArrow = 0;
    int textFirst = -1;
    // Each shown row's stepper: the room between its arrows (kChipW, or on
    // cards as much as its value needs), set with the rows' texts.
    int stepperMiddle[kMaxVisible + 1] = {};
    int StepperWidth(int row) const;
    bool dirty = true;
    bool cards = false;
    bool keysActive = true;
    int rowH = kRowHeight;
    int CardLeft(const FlowRow& r) const;
    int CardRight() const;
    GuiText* label[kMaxVisible + 1];
    GuiText* value[kMaxVisible + 1];
    GuiText* detail[kMaxVisible + 1];
    GuiText* tag[kMaxVisible + 1];
    // The top line a row's label and control share (SetNotesUnder).
    int LineHeight(const FlowRow& r) const { return notesUnder && !r.detail.empty() ? 34 : rowH; }  // a stepper's arrows tall
    GuiSound* soundOver;
    GuiSound* soundClick;
};
