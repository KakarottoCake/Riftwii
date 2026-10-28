// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The menu's shared motion (include/riftwii/menumotion.hpp): the same
// movement after the same time on a 50 Hz or a 60 Hz display.
#include <cmath>
#include <iostream>

#include "riftwii/menumotion.hpp"

static int g_failures = 0;
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; ++g_failures; } } while (0)

// Home's motion must reach the same place after the same time on a PAL
// or NTSC display, including a change of target while it is still moving.
static void test_motion_timing() {
    namespace motion = riftwii::menumotion;
    for (float response : {40.0f, 47.0f, 58.0f, 75.0f, 130.0f, 200.0f}) {
        const auto simulate = [&](int hz) {
            float value = 0;
            for (int frame = 0; frame < hz; ++frame) {
                const float target = frame < hz / 2 ? 100.0f : -20.0f;
                value = motion::follow(value, target, 1000.0f / hz, response, 0.0f);
            }
            return value;
        };
        EXPECT_EQ(std::fabs(simulate(50) - simulate(60)) < 0.001f, true);

        const auto flick = [&](int hz) {
            float velocity = 24.0f;
            float distance = 0;
            for (int frame = 0; frame < hz; ++frame)
                distance += motion::coast(velocity, 1000.0f / hz, response);
            return distance;
        };
        EXPECT_EQ(std::fabs(flick(50) - flick(60)) < 0.001f, true);
    }
    float highlight = 0;
    for (int frame = 0; frame < 60; ++frame)
        highlight = motion::follow(highlight, 1.0f, motion::kFrameMs, 47.0f);
    EXPECT_EQ(highlight, 1.0f);
    for (int frame = 0; frame < 60; ++frame)
        highlight = motion::follow(highlight, 0.0f, motion::kFrameMs, 47.0f);
    EXPECT_EQ(highlight, 0.0f);
    EXPECT_EQ(motion::follow(0.4f, 0.0f, 0.0f, 47.0f), 0.4f);
    const float reversed = motion::follow(0.4f, 0.0f, 20.0f, 47.0f);
    EXPECT_EQ(reversed > 0.0f && reversed < 0.4f, true);
    EXPECT_EQ(motion::frame_ms(1000.0f), 50.0f);
    EXPECT_EQ(motion::frame_ms(20.0f, 4.0f), 5.0f);
    EXPECT_EQ(motion::frame_ms(-1.0f), 0.0f);
    for (const auto curve : {motion::ease, motion::ease_out, motion::ease_back}) {
        EXPECT_EQ(curve(0.0f), 0.0f);
        EXPECT_EQ(curve(1.0f), 1.0f);
        EXPECT_EQ(curve(-1.0f), 0.0f);
        EXPECT_EQ(curve(2.0f), 1.0f);
    }
}

static void test_banner_timing() {
    namespace motion = riftwii::menumotion;
    for (int hz : {50, 60}) {
        float frame = 0;
        for (int draw = 0; draw < hz; ++draw)
            frame = motion::animation_frame(frame, 1000.0f / hz, 10, 37);
        EXPECT_EQ(std::fabs(frame - 23.0f) < 0.001f, true);
    }
    EXPECT_EQ(motion::animation_frame(0.0f, 0.0f, 10, 37), 0.0f);
    EXPECT_EQ(motion::animation_frame(10.0f, 1000.0f, 10, 0), 10.0f);
    EXPECT_EQ(motion::animation_frame(0.0f, 1000.0f, 0, 0), 0.0f);
    const float subframe = motion::animation_frame(0.0f, 10.0f, 10, 37);
    EXPECT_EQ(subframe > 0.5f && subframe < 0.7f, true);
}

int main() {
    test_motion_timing();
    test_banner_timing();
    if (g_failures != 0) return 1;
    std::cout << "ALL MENUMOTION TESTS PASSED" << std::endl;
    return 0;
}
