// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"

// The home screen's games: a page of 12 tiles like the Wii Menu's
// channels, either names (4x3 wide tiles) or box art (6x2 covers, the
// lit game's name under them; a game without a cover shows its name).
// Point and press A, or move the focus with the D-pad; stepping past the
// left or right column, or the arrows at the sides, turns the page. The
// widget covers the whole screen and lays tiles out itself.
struct GridItem {
    std::string title;  // the display name, fitted to the tile
    std::string id;     // shown small under the title; the cover's game
    std::string badge;  // DISC, USB, SD
    bool mods = false;  // packs exist for this game
    GXColor hue = {90, 90, 100, 255};
};

class GuiGameGrid : public GuiElement {
public:
    static constexpr int kPerPage = 12;

    GuiGameGrid();
    ~GuiGameGrid() override;
    void SetItems(const std::vector<GridItem>* items);  // must outlive the grid
    // Covers (6x2) or names (4x3).
    void SetCovers(bool on);
    // A cover was stored for `id`: draw it from now on.
    void CoverArrived(const std::string& id);
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
        GuiText* lines[3];
        GuiText* id;
        GuiText* badge;
        GuiText* mods;
        float scale = 1.0f;
    };
    struct Geometry {
        int cols, tileW, tileH, left, top, gapX, gapY;
    };
    const Geometry& Geo() const;
    int Cols() const { return Geo().cols; }
    int TileX(int slot) const;
    int TileY(int slot) const;
    int ArrowY() const;
    int Count() const { return items ? static_cast<int>(items->size()) : 0; }
    // `text` in at most `lines` lines of `width` at the measure's size; the
    // last one cut with an ellipsis when it is longer still.
    std::vector<std::string> Wrap(const std::string& text, int width, int lines);
    std::string Fit(const std::string& text, int width);
    void Layout();  // fits the visible titles
    void TurnPage(int delta);
    int SlotAt(int x, int y) const;
    int ArrowAt(int x, int y) const;  // -1 left, +1 right, 0 none
    void DrawNameTile(int i, bool on, int alpha);
    void DrawCoverTile(int i, bool on, int alpha);

    const std::vector<GridItem>* items = nullptr;
    Slot slots[kPerPage];
    GuiText* measure;
    GuiText* caption;    // covers: the lit game's name
    int captionFor = -1;
    GuiSound* soundOver;
    GuiSound* soundClick;
    bool covers = false;
    int page = 0;
    int focus = 0;       // item index
    int hover = -1;      // slot under a pointer, -1 when none
    int arrowHover = 0;
    int clicked = -1;
    bool laidOut = false;
};
