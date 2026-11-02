// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>

#include "spinetext.hpp"

namespace {

int failures = 0;
constexpr float kNormalLength = 164.0f * 0.78f;

void Expect(bool condition, const char* description) {
    if (condition) return;
    std::fprintf(stderr, "FAILED: %s\n", description);
    ++failures;
}

void TestProjectedBounds() {
    namespace spine = riftwii::wii;
    // The fit sees menu coordinates after projection. The same bounds
    // must hold on a wide TV and a 4:3 TV, including a box turning away.
    for (float widthScale : {0.75f, 1.0f, 1.25f}) {
        int previousRoom = 0;
        for (int step = 160; step >= 1; --step) {
            const float scale = step / 100.0f;
            const float length = kNormalLength * scale;
            const float width = 14.0f * scale * widthScale;
            const auto layout = spine::FitSpineTextLayout(length, width, kNormalLength, previousRoom);
            previousRoom = layout.room;
            if (layout.fontSize == 0) continue;
            Expect(layout.room <= length - 2 * spine::kSpineTextMargin, "name stays between the end margins");
            Expect(layout.fontSize + 2 <= width, "rotated letters stay inside the spine's width");
            Expect(layout.fontSize >= spine::kSpineTextMinSize, "visible letters keep the readable size floor");
            Expect(layout.fontSize <= spine::kSpineTextSize, "nearby boxes do not enlarge the font");
            Expect(layout.room >= layout.fontSize, "a visible label has room for a letter");
        }
    }
    const auto normal = spine::FitSpineTextLayout(kNormalLength, 16, kNormalLength, 0);
    const auto small = spine::FitSpineTextLayout(kNormalLength * 0.8f, 12, kNormalLength, 0);
    Expect(small.room < normal.room, "distant boxes shorten their names");
    Expect(small.fontSize < normal.fontSize, "distant boxes shrink their letters");
    Expect(spine::FitSpineTextLayout(12, 16, kNormalLength, 0).fontSize == 0, "a short spine has no name");
    Expect(spine::FitSpineTextLayout(100, 10, kNormalLength, 0).fontSize == 0, "a narrow spine has no name");
    Expect(spine::FitSpineTextLayout(0, 0, kNormalLength, 0).fontSize == 0, "a hidden spine has no name");
    Expect(spine::FitSpineTextLayout(-10, 16, kNormalLength, 0).fontSize == 0, "an inverted spine has no name");
    Expect(spine::FitSpineTextLayout(100, 16, 8, 0).fontSize == 0, "an invalid reference length has no name");
}

void TestStableRoom() {
    namespace spine = riftwii::wii;
    auto layout = spine::FitSpineTextLayout(112.1f, 16, kNormalLength, 0);
    Expect(layout.room == 104, "room rounds down to keep the margins");
    layout = spine::FitSpineTextLayout(111.9f, 16, kNormalLength, layout.room);
    Expect(layout.room == 100, "a shrinking spine immediately drops an unsafe fit");
    for (float length : {112.1f, 111.9f, 112.2f, 115.9f}) {
        layout = spine::FitSpineTextLayout(length, 16, kNormalLength, layout.room);
        Expect(layout.room == 100, "small motion cannot alternate truncation lengths");
    }
    layout = spine::FitSpineTextLayout(116.0f, 16, kNormalLength, layout.room);
    Expect(layout.room == 108, "a growing spine eventually gets a longer name");
}

}  // namespace

int main() {
    TestProjectedBounds();
    TestStableRoom();
    if (failures != 0) return 1;
    std::puts("ALL SPINE TEXT TESTS PASSED");
    return 0;
}
