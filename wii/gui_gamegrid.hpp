// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"

// The home screen's games: a 4x3 page of tiles like the Wii Menu's
// channels. Point and press A, or move the focus with the D-pad; stepping
// past the left or right column, or the arrows at the sides, turns the
// page. The widget covers the whole screen and lays tiles out itself.
struct GridItem {
    std::string title;  // the display name, fitted to two lines
    std::string id;     // shown small under the title
    std::string badge;  // DISC, USB, SD
    bool mods = false;  // packs exist for this game
    GXColor hue = {90, 90, 100, 255};
};

class GuiGameGrid : public GuiElement {
public:
    static constexpr int kCols = 4, kRows = 3, kPerPage = kCols * kRows;
    static constexpr int kLeft = 34, kTop = 20, kTileW = 134, kTileH = 84, kGap = 12;

    GuiGameGrid();
    ~GuiGameGrid() override;
    void SetItems(const std::vector<GridItem>* items);  // must outlive the grid
    // Keeps the focus on `index` (and its page).
    void Focus(int index);
    int FocusedIndex() const { return focus; }
    int Page() const { return page; }
    int Pages() const;
    // The item A was pressed on since the last call, or -1.
    int GetClicked();
    void Draw() override;
    void Update(GuiTrigger* t) override;

private:
    struct Slot {
        GuiText* line1;
        GuiText* line2;
        GuiText* id;
        GuiText* badge;
        GuiText* mods;
        float scale = 1.0f;
    };
    int Count() const { return items ? static_cast<int>(items->size()) : 0; }
    void Layout();  // fits the visible titles
    void TurnPage(int delta);
    int SlotAt(int x, int y) const;
    int ArrowAt(int x, int y) const;  // -1 left, +1 right, 0 none

    const std::vector<GridItem>* items = nullptr;
    Slot slots[kPerPage];
    GuiText* measure;
    GuiSound* soundOver;
    GuiSound* soundClick;
    int page = 0;
    int focus = 0;       // item index
    int hover = -1;      // slot under a pointer, -1 when none
    int arrowHover = 0;
    int clicked = -1;
    bool laidOut = false;
};
