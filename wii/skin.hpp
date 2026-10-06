// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

#include "libwiigui/gui.h"

// The menu's look: a light, Wii-Menu-like theme drawn by RiftWii itself.
// Every panel, tile, button, icon and the pointer is painted once at start
// on the software canvas (riftwii/canvas.hpp) into GX textures in MEM2, so
// the menu ships no artwork files and the GPU only blits. A theme
// (wii/menutheme.hpp, docs/THEMES.md) changes the colours below and may
// replace any painted picture with its own PNG.
namespace riftwii::wii::skin {

// The theme's colours: RiftWii's own until Init() reads the theme.
extern GXColor kInk, kInkSoft, kInkDim, kClock, kAccent, kAccentInk, kWarn;
extern GXColor kTextOnAccent;  // text on the accent or on a game's colour
extern GXColor kBar, kDivider, kScrollTrack, kScrollThumb, kBadge;
extern GXColor kShelfWood, kShelfEdge;  // Home's shelf: the plank's top and front
extern GXColor kChipOn, kChipOff, kChipOffEdge;  // the option chips' (and the search keys') fill and edge
// Always white (a QR code's background).
constexpr GXColor kWhite = {255, 255, 255, 255};

struct Tex {
    u8* data = nullptr;
    int w = 0, h = 0;
};

// Built by Init(); each has a transparent margin for its shadow or glow.
extern Tex tile, tileOver;               // 134x84 game tiles, drawn at -7,-7
extern Tex coverTile, coverTileOver;     // 80x112 cover tiles, drawn at -7,-7
extern Tex shelfPlank;                   // 256x64 RGBA8: the plank's top, then its front edge
extern Tex roundBtn, roundBtnOver;       // 76 round buttons, drawn at -2,-2
extern Tex pill, pillOver;               // 244x52 buttons, drawn at -4,-4
extern Tex pillPrimary, pillPrimaryOver;
extern Tex homeBtn, homeBtnOver;        // 248x72 HOME Menu buttons, drawn at -8,-8
extern Tex chipOff, chipOn;              // 208x30 option chips, drawn at -2,-3
extern Tex rowFocus;                     // 548x44 highlighted list row
extern Tex stepBack, stepBackOver, stepForward, stepForwardOver;  // 34 round arrow buttons, drawn at -4,-4
extern Tex switchOn, switchOff;          // 60x30 On/Off switches, drawn at -3,-4
extern Tex panelGame, panelSettings;     // 572x232 and 572x276 white panels, drawn at -4,-4
extern Tex bar;                          // 640x124 bottom bar
extern Tex bannerStripes;                // 640x192 overlay for the game banner
extern Tex arrowLeft, arrowLeftOver, arrowRight, arrowRightOver;  // 44 page arrows, drawn at -2,-2
extern Tex scrollUp, scrollUpOver, scrollDown, scrollDownOver;    // 34 list scroll arrows, drawn at -4,-4
extern Tex iconDrives, iconGear, iconSearch;  // 28x28
extern Tex iconDisc;                     // 40x40, the Disc drive tile's picture
extern Tex hand[4];                      // 96x96 pointers, fingertip at the centre
extern Tex background;                   // 640x480, a theme's (else none: GuiBackdrop paints)
// 856 across, a theme's for a widescreen menu (else none: the 640 ones are
// mirrored outward, DrawExtended).
extern Tex backgroundWide, barWide;

// Takes `bytes` of MEM2, 32-byte aligned, for the rest of the menu phase;
// nullptr when MEM2 is full.
u8* Mem2Alloc(std::size_t bytes);

// A hover name's rounded box (riftwii/skinpaint.hpp), `w` x `h` plus
// its shadow's margin; painted once per size and kept.
Tex HintBox(int w, int h);
// The Mods page's picture popup's card, `w` x `h` plus the same margin.
Tex ArtFrame(int w, int h);

// Reads the theme (wii/menutheme.hpp), then paints or loads every picture.
void Init();
bool Ready();

// A colour of the tile palette for a game ID (stable across runs).
GXColor HueFor(const std::string& id);

// `alpha` 0-255; `scale` about the texture's centre.
void Draw(const Tex& t, float x, float y, int alpha = 255, float scale = 1.0f);
GXColor WithAlpha(GXColor c, int alpha);
// A w x h RGB5A3 texture (a cover, wii/covers.hpp), like Draw.
void DrawRgb5a3(const u8* data, int w, int h, float x, float y, int alpha = 255, float scale = 1.0f);

// Whether the menu is drawn for a 16:9 TV (Menu_SafeArea wider than 640).
bool WideMenu();

// `t` at x, y, 1:1, and the rectangle [left, right) x [top, bottom) around
// it filled with mirror images of its edges: its first and last `bandX`
// columns going outward, back and forth, then its first and last `bandY`
// rows the same way. Mirrored, an edge meets its own copy, so no seam
// shows and nothing is stretched. A band of 0 leaves that side empty.
void DrawExtended(const Tex& t, float x, float y, float left, float top, float right, float bottom, int bandX,
                  int bandY);

// The striped light background, drawn under every screen.
class GuiBackdrop : public GuiElement {
public:
    GuiBackdrop();
    void Draw() override;
};

}  // namespace riftwii::wii::skin
