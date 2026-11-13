// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "libwiigui/gui.h"

namespace riftwii::wii {

// Small enough to sit beside a wait message. Show is called with the GUI
// halted; time only advances while the ring is actually being drawn.
class BusyRing : public GuiElement {
public:
    // Position is the ring's centre, relative to its parent and alignment.
    BusyRing(int x, int y, bool active = true) : active_(active) {
        SetPosition(x, y);
    }
    void Show(bool active);
    void SetActive(bool active) { Show(active); }
    void Draw() override;

private:
    bool active_ = true;
    float elapsed_ = 0;
};

void DrawPill(float x, float y, float width, int height, GXColor colour);

// The left `shown` pixels of a pill `width` wide, cut straight on the right:
// a bar's fill while it is shorter than the track's rounded end, so it stays
// inside the track's outline instead of standing out as a tall sliver.
void DrawPillStart(float x, float y, float width, float shown, int height, GXColor colour);

void DrawBusyRing(float x, float y, float elapsedMs, int alpha = 255);

}  // namespace riftwii::wii
