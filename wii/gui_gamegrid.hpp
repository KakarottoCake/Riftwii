// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "libwiigui/gui.h"

// The home screen's games: a page of 12 tiles like the Wii Menu's
// channels, either names (4x3 wide tiles) or box art (6x2 covers, the
// lit game's name under them; a game without a cover shows its name), or
// a shelf of game boxes standing spine out, the focused one pulled out
// and turned to show its cover (wii/gui_shelf.cpp).
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
    // The shelf instead of either (covers drawn on its boxes' fronts).
    void SetShelf(bool on);
    bool Shelf() const { return shelf; }
    // The shelf's games near the focus, nearest first: whose boxes to fetch.
    std::vector<int> ShelfWanted() const;
    // A box was stored for `id`: draw it from now on.
    void BoxArrived(const std::string& id);
    // Channels: the names' 4x3 tiles, each showing the game's own animated
    // icon from its banner when `icon` draws one (item index, the box to
    // fill in screen pixels, alpha); a game without one shows its name.
    struct IconBox {
        float x, y, w, h;                         // where the icon goes (may stick out of the tile)
        float clipX, clipY, clipW, clipH, radius;  // the tile's inside, with its round corners
        int alpha;
    };
    using IconDrawer = std::function<bool(int index, const IconBox& box)>;
    void SetChannels(bool on, IconDrawer icon);
    bool Channels() const { return channels; }
    // The items on the page shown: whose icons to have ready.
    int PageFirst() const { return page * kPerPage; }
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
    // Letters, as WiiFlow does: B held (L on a GameCube controller) with
    // right or left goes to the first game of the next or the previous
    // letter, wrapping round; with down or up, a page. B pressed alone goes
    // to the next letter when it is let go. True while B is held: the
    // directions are the letters', not the focus's.
    bool LetterKeys(GuiTrigger* t);
    int LetterTarget(int dir) const;
    bool letterHeld[4] = {};
    bool letterUsed[4] = {};
    int SlotAt(int x, int y) const;
    int ArrowAt(int x, int y) const;  // -1 left, +1 right, 0 none
    void DrawNameTile(int i, bool on, int alpha);
    void DrawCoverTile(int i, bool on, int alpha);
    void PrefetchCovers();
    void ReadAllCovers();
    // The shelf (wii/gui_shelf.cpp).
    struct BoxPose {
        float x = 320.0f, z = 0.0f, turn = 0.0f, lift = 0.0f;
        bool placed = false;
    };
    struct SpineRect {
        int index;
        float x0, y0, x1, y1;
    };
    void DrawShelf(int alpha);
    void UpdateShelf(GuiTrigger* t);
    void ShelfStep(int delta);
    int ShelfHit(int x, int y) const;
    std::vector<BoxPose> poses;
    std::vector<SpineRect> spineRects;  // the boxes as last drawn, nearest last
    // A box without its art: where its name goes up its spine.
    struct SpineLabel {
        int index;
        float x, y0, y1;
    };
    // Reused every frame: the boxes in drawing order, the names to write.
    std::vector<int> shelfOrder;
    std::vector<SpineLabel> spineLabels;
    GuiText* spineText = nullptr;       // a box without its art: the name up its spine
    // Each spine's name as fitted, with the title it was fitted from.
    std::vector<std::pair<std::string, std::string>> spineFit;
    bool shelf = false;
    int shelfHover = -1;                // the box a pointer is on
    bool channels = false;
    IconDrawer iconDrawer;
    bool iconShown[kPerPage] = {};  // channels: the tile drew its icon this frame
    void DrawChannelLabel(int slot, int alpha);
    // What ReadAllCovers last gave the loader.
    const std::vector<GridItem>* readAllItems = nullptr;
    int readAllCount = -1;
    std::string readAllFirst, readAllLast;
    int turning = 1;  // the way the pages last turned: +1 or -1

    const std::vector<GridItem>* items = nullptr;
    Slot slots[kPerPage];
    GuiText* measure;
    GuiText* caption;    // covers: the lit game's name
    int captionFor = -1;
    GuiSound* soundOver;
    GuiSound* soundClick;
    bool covers = false;
    int page = 0;
    float slide = 0.0f;  // the page's offset while it slides in
    int focus = 0;       // item index
    int hover = -1;      // slot under a pointer, -1 when none
    int arrowHover = 0;
    int clicked = -1;
    bool laidOut = false;
};
