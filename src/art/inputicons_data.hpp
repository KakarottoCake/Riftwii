// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Kenney's Input Prompts pictures as alpha masks (inputicons_data.cpp, made
// by tools/make_input_icons.py). Internal to src/art/inputicons.cpp.
namespace riftwii {

constexpr int kInputIconSource = 64;  // each mask is 64 x 64

struct InputIconData {
    const char* name;            // the picture's file name without ".png"
    const unsigned char* alpha;  // kInputIconSource * kInputIconSource bytes
};
extern const InputIconData kInputIcons[];
extern const unsigned kInputIconCount;

}  // namespace riftwii
