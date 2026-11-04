// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii::skin { struct Tex; }

namespace riftwii::wii {

// One hover's arrival and departure, retaining its position if interrupted.
// Amount may briefly pass 1; opacity should use min(1, Amount()).
class HoverSwell {
public:
    float Advance(bool selected);
    float Amount() const { return amount_; }

private:
    float amount_ = 0;
    float from_ = 0;
    float elapsed_ = 0;
    bool selected_ = false;
};

// Wide buttons need less growth to leave their neighbours' text clear.
float ButtonSwellScale(float width, float amount);

// A quiet sweep every 3.5 seconds, inside the button's rounded body.
// Draw before its label, under the same camera as the button.
void DrawButtonGlint(float x, float y, float w, float h, float radius, int alpha = 255);

// Direction points out of the page. The invitation moves inward, keeping
// an edge arrow inside the safe area; its hit area stays still.
void DrawInvitingArrow(const skin::Tex& plain, const skin::Tex& over,
                      float x, float y, int dx, int dy, bool lit,
                      float& lift, int alpha = 255);

}  // namespace riftwii::wii
