// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "libwiigui/gui.h"
#include "skin.hpp"

// The home screen's games: a page of 12 tiles like the Wii Menu's
// channels, either names (4x3 wide tiles) or box art (6x2 covers, the
// lit game's name under them; a game without a cover shows its name), or
// a shelf of game boxes standing spine out, the focused one pulled out
// and turned to show its cover (wii/menu/gui_shelf.cpp).
// Point and press A, or move the focus with the D-pad; stepping past the
// left or right column, or the arrows at the sides, turns the page. The
// widget covers the whole screen and lays tiles out itself.
// A game's box on the shelf, in the shelf's own terms: where it stands
// (x along the shelf, z out of it), how far it is turned and lifted.
struct ShelfFlight {
    std::string id;
    GXColor hue = {0, 0, 0, 255};
    float x = 0, z = 0, turn = 0, lift = 0;
};
// The box flying between the shelf and a spot on screen (the game page's
// cover, toX..toH in menu units), `e` of the way (0 to 1; `reverse`: from
// the spot back to `from`). Drawn over everything, then the 2D view back.
void DrawShelfFlight(const ShelfFlight& from, float toX, float toY, float toW, float toH, float e, int alpha,
                     bool reverse);
// The focused box's place (where a game coming back from its page lands).
ShelfFlight ShelfFocusedPose(const std::string& id, GXColor hue);
// The shelf leaves this game's place empty while its box flies ("" none).
void SetShelfFlying(const std::string& id);

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
    // With the shelf on: its boxes fanned out along an arc, every one
    // showing its front, the focused one forward in the middle (no plank).
    void SetCarousel(bool on);
    // With the shelf on: GameTDB's 3D covers (pictures of the cases, not
    // models) standing round a ring that never ends: past the last game
    // comes the first. The focused one stands large in front.
    void SetRing(bool on);
    bool Ring() const { return ring; }
    // A 3D cover was stored for `id`: draw it from now on.
    void Cover3dArrived(const std::string& id);
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
    // Channels: the neighbouring pages' nearest columns at the screen's
    // sides (page_peek), read from the settings when Home is built: the
    // GUI thread asks every frame, so not from the settings' string.
    void SetPagePeek(bool on) { pagePeek = on; }
    // The items on the page shown: whose icons to have ready.
    int PageFirst() const { return page * kPerPage; }
    // A cover was stored for `id`: draw it from now on.
    void CoverArrived(const std::string& id);
    // Keeps the focus on `index` (and its page).
    void Focus(int index);
    int FocusedIndex() const { return focus; }
    // Where a game's tile (or box, on the shelf) is on screen, when it is
    // shown: what a transition zooms from.
    bool TileRect(int index, float& x, float& y, float& w, float& h) const;
    bool ShelfFlightFrom(int index, ShelfFlight& out) const;  // the box as last drawn
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
    float Stretch() const;          // how much wider Channels is drawn on a widescreen menu (1 otherwise)
    mutable Geometry stretched{};   // Geo()'s, when stretched
    mutable Geometry coverGeo{};    // Geo()'s for covers (their size follows the menu's width)
    mutable Geometry channelGeo{};  // Geo()'s for channels (in pixels: the icons drawn one texel to a pixel)
    int CoverCaptionY() const;      // the lit cover's name, under the covers
    int ArrowX(int dir) const;      // a page arrow's left edge (-1 left, +1 right)
    void DrawTilePicture(const riftwii::wii::skin::Tex& t, float x, float y, int alpha, float scale) const;
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
    int shelfStepHeld[4] = {};  // frames + or - has been held on the shelf, per controller
    bool letterUsed[4] = {};
    int SlotAt(int x, int y) const;
    int ArrowAt(int x, int y) const;  // -1 left, +1 right, 0 none
    void DrawNameTile(int index, Slot& s, float x, float y, bool on, int alpha);
    // Channels: the game's icon (or the disc drive's disc) in its channel
    // at (x, y); false when it has neither (its name is drawn instead).
    bool DrawChannelTile(int index, float x, float y, float scale, bool on, int alpha);
    // Channels, page_peek on: the pages either side, their nearest column
    // showing at the screen's sides, dimmed, carried along by a page turn.
    bool PagePeek() const;  // SetPagePeek's, in the Channels view only
    float PageStride() const;  // one page to the next, in menu units
    int PeekAlpha(float x, int alpha) const;  // a tile at x: dimmed the further out of the page's columns
    void DrawNeighbourPages(int alpha);
    void DrawCoverTile(int i, bool on, int alpha);
    void PrefetchCovers();
    void ReadAllCovers();
    // The shelf (wii/menu/gui_shelf.cpp).
    struct BoxPose {
        float x = 320.0f, z = 0.0f, turn = 0.0f, lift = 0.0f;
        bool placed = false;
    };
    struct SpineRect {
        int index;
        float x0, y0, x1, y1;
    };
    void DrawShelf(int alpha);
    void DrawRing(int alpha);
    void DrawShelfCaption(int alpha);
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
    std::vector<int> shelfOutward;  // the boxes in reach, from the focus outwards (DrawShelf)
    std::vector<SpineLabel> spineLabels;
    GuiText* spineText = nullptr;       // a box without its art: the name up its spine
    // Each spine's name as fitted, with the title it was fitted from.
    std::vector<std::pair<std::string, std::string>> spineFit;
    bool shelf = false;
    bool carousel = false;
    bool ring = false;
    float ringPos = 0.0f;  // the ring's turn, in games (0 to Count()), following the focus
    bool ringPlaced = false;
    int shelfHover = -1;                // the box a pointer is on
    // The focused box turned round to its back (Up or Down), for that box
    // only; how many frames the focus has stood still (its sway and the
    // backdrop wait for it); the frame count the sway runs on.
    bool shelfFlip = false;
    int shelfFlipFor = -1;
    int shelfStill = 0;
    unsigned shelfClock = 0;
    // A held direction speeds up: the steps that came close together.
    u64 shelfLastStep = 0;
    int shelfStreak = 0;
    // The pointer with A held drags the shelf sideways; let go moving, it
    // glides on and slows down.
    bool dragArmed = false, dragging = false;
    int dragPress = -1;  // the box A went down on
    int dragChan = -1;   // the controller dragging (Update runs once for each)
    float dragStartX = 0, dragLastX = 0, dragAccum = 0, dragSpeed = 0;
    float glide = 0, glideAccum = 0;
    GuiText* captionInfo = nullptr;  // under the shelf's name: where the game is, its region, play count, mods
    int infoFor = -1;
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
    // The page turned away from, its names as laid out (Channels with
    // page_peek on draws it sliding away, then beside the page).
    Slot leaving[kPerPage];
    int leavingPage = -1;
    bool pagePeek = false;
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
