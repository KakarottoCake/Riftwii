// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <vector>

#include "libwiigui/gui.h"
#include "skin.hpp"

// Tiles for the game page and Settings: large cards placed anywhere on
// the screen, each with an icon, a name and its current value, in place of
// a long list. One is lit (an accent ring and a slight lift, never colour
// alone); the D-pad moves to the nearest tile that way, the pointer lights
// the one it is on, and A acts on the lit one. A primary tile (Play) is
// tinted so the eye finds it first.
struct TileSpec {
    int x = 0, y = 0, w = 0, h = 0;  // on the 640x480 screen
    std::string label;
    std::string value;                          // under the name, smaller; empty for none
    const riftwii::wii::skin::Tex* icon = nullptr;  // 28x28
    bool primary = false;
    bool on = false;   // the value in the accent's colour (something switched on)
    bool dim = false;  // greyed: still lit and pressed, its page says why
    bool warn = false; // the value in the theme's warning colour (a memory card that is full)
};

class GuiTiles : public GuiElement {
public:
    GuiTiles();
    ~GuiTiles() override;
    void SetTiles(const std::vector<TileSpec>* tiles);  // must outlive the tiles
    void Refresh();                                     // after the tiles' texts changed
    void Select(int index);
    // The lit tile, or the one the D-pad lit last while the pointer is
    // off the tiles.
    int Selected() const { return hover >= 0 ? hover : focus; }
    // The tile A was pressed on since the last call; -1 when none.
    int GetClicked();
    void Draw() override;
    void Update(GuiTrigger* t) override;

private:
    int Count() const { return tiles ? static_cast<int>(tiles->size()) : 0; }
    int TileAt(int x, int y) const;
    int Neighbour(int from, int dx, int dy) const;
    void Layout(int index);

    const std::vector<TileSpec>* tiles = nullptr;
    int focus = 0, hover = -1, clicked = -1;
    std::vector<float> lift;  // each tile's lit look, eased 0 to 1
    std::vector<GuiText*> label;
    std::vector<GuiText*> value;
    bool dirty = true;
    GuiSound* soundOver;
    GuiSound* soundClick;
};
