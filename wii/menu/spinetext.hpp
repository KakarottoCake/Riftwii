// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace riftwii::wii {

constexpr int kSpineTextSize = 13;
constexpr int kSpineTextMinSize = 9;
constexpr int kSpineTextMargin = 4;

struct SpineTextLayout {
    int room = 0;
    int fontSize = 0;
};

// Length and width are already projected into menu pixels, in either TV
// shape. normalLength is the label's length on a box at its original size.
inline SpineTextLayout FitSpineTextLayout(float length, float width, float normalLength, int previousRoom) {
    constexpr int kRoomStep = 4;
    const float available = length - 2 * kSpineTextMargin;
    if (available < kSpineTextMinSize || width < kSpineTextMinSize + 2) return {};

    int room = static_cast<int>(std::floor(available / kRoomStep)) * kRoomStep;
    // Shrink as soon as the old fit would leave the spine. Growing waits
    // one extra step, so tiny motion at a boundary cannot alternate fits.
    if (previousRoom > 0 && previousRoom <= room && room < previousRoom + 2 * kRoomStep) {
        room = previousRoom;
    }

    const float normalRoom = normalLength - 2 * kSpineTextMargin;
    if (normalRoom <= 0) return {};
    const int scaledSize = static_cast<int>(std::floor(kSpineTextSize * room / normalRoom));
    const int widthLimit = static_cast<int>(std::floor(width - 2));
    const int fontSize = std::min(widthLimit, std::clamp(scaledSize, kSpineTextMinSize, kSpineTextSize));
    if (room < fontSize) return {};
    return {room, fontSize};
}

}  // namespace riftwii::wii
