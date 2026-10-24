// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii {

struct HomeShoulderInput {
    bool left_pressed = false;
    bool right_pressed = false;
    bool left_held = false;
    bool right_held = false;
    bool left_used = false;  // L already moved a tile or performed a letter jump
};

struct HomeShoulderPages {
    bool chord = false;

    int update(const HomeShoulderInput& input) {
        // ScreenshotPoll delays GameCube clicks until release. Remember a
        // two-trigger chord through either release, even without Down.
        const bool left = input.left_pressed || input.left_held;
        const bool right = input.right_pressed || input.right_held;
        const bool blocked = chord || (left && right);
        chord = blocked && (input.left_held || input.right_held);
        if (blocked) return 0;
        if (input.left_pressed && !input.left_used) return -1;
        if (input.right_pressed) return 1;
        return 0;
    }
};

}  // namespace riftwii
