// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "libwiigui/gui.h"

// The menu's look: a light, Wii-Menu-like theme drawn by Riftwii itself.
// Every panel, tile, button, icon and the pointer is painted once at start
// on the software canvas (riftwii/canvas.hpp) into GX textures in MEM2, so
// the menu ships no artwork files and the GPU only blits.
namespace riftwii::wii::skin {

constexpr GXColor kInk = {46, 46, 54, 255};
constexpr GXColor kInkSoft = {74, 74, 84, 255};
constexpr GXColor kInkDim = {106, 106, 116, 255};
constexpr GXColor kClock = {116, 116, 126, 255};
constexpr GXColor kAccent = {47, 182, 233, 255};
constexpr GXColor kAccentInk = {14, 100, 136, 255};
constexpr GXColor kWhite = {255, 255, 255, 255};
constexpr GXColor kWarn = {176, 58, 46, 255};

struct Tex {
    u8* data = nullptr;
    int w = 0, h = 0;
};

// Built by Init(); each has a transparent margin for its shadow or glow.
extern Tex tile, tileOver;               // 134x84 game tiles, drawn at -7,-7
extern Tex roundBtn, roundBtnOver;       // 76 round buttons, drawn at -2,-2
extern Tex pill, pillOver;               // 244x52 buttons, drawn at -4,-4
extern Tex pillPrimary, pillPrimaryOver;
extern Tex chipOff, chipOn;              // 176x30 option chips, drawn at -2,-3
extern Tex rowFocus;                     // 548x34 highlighted list row
extern Tex panelGame, panelSettings;     // 572x176 and 572x252 white panels, drawn at -4,-4
extern Tex bar;                          // 640x124 bottom bar
extern Tex bannerStripes;                // 640x192 overlay for the game banner
extern Tex arrowLeft, arrowLeftOver, arrowRight, arrowRightOver;  // 44 page arrows, drawn at -2,-2
extern Tex iconDrives, iconGear;         // 28x28
extern Tex hand[4];                      // 96x96 pointers, fingertip at the centre

void Init();
bool Ready();

// A colour of the tile palette for a game ID (stable across runs).
GXColor HueFor(const std::string& id);

// `alpha` 0-255; `scale` about the texture's centre.
void Draw(const Tex& t, float x, float y, int alpha = 255, float scale = 1.0f);
GXColor WithAlpha(GXColor c, int alpha);

// The striped light background, drawn under every screen.
class GuiBackdrop : public GuiElement {
public:
    GuiBackdrop();
    void Draw() override;
};

}  // namespace riftwii::wii::skin
