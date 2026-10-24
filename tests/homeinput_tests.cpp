// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>

#include "riftwii/homeinput.hpp"

static int g_failures = 0;
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; ++g_failures; } } while (0)

using namespace riftwii;

static void test_gamecube_clicks() {
    HomeShoulderPages state;
    HomeShoulderInput left_held;
    left_held.left_held = true;
    HomeShoulderInput left_released;
    left_released.left_pressed = true;
    HomeShoulderInput right_held;
    right_held.right_held = true;
    HomeShoulderInput right_released;
    right_released.right_pressed = true;
    // The screenshot handler withholds down until the digital click is
    // released. Holding a trigger, or an analogue-only pull, cannot page.
    EXPECT_EQ(state.update({}), 0);
    EXPECT_EQ(state.update(left_held), 0);
    EXPECT_EQ(state.update(left_released), -1);
    EXPECT_EQ(state.update({}), 0);
    EXPECT_EQ(state.update(right_held), 0);
    EXPECT_EQ(state.update(right_released), 1);
    // A D-pad letter jump or tile move has already used L.
    EXPECT_EQ(state.update(left_held), 0);
    left_held.left_used = true;
    left_released.left_used = true;
    EXPECT_EQ(state.update(left_held), 0);
    EXPECT_EQ(state.update(left_released), 0);
    // A subsequent unused click works in any Home ordering.
    left_held.left_used = false;
    left_released.left_used = false;
    EXPECT_EQ(state.update(left_held), 0);
    EXPECT_EQ(state.update(left_released), -1);
}

static void test_chord_releases() {
    HomeShoulderInput both_held;
    both_held.left_held = both_held.right_held = true;
    for (bool left_first : {false, true}) {
        for (bool screenshot : {false, true}) {
            HomeShoulderPages state;
            HomeShoulderInput first_held;
            first_held.left_held = left_first;
            first_held.right_held = !left_first;
            EXPECT_EQ(state.update(first_held), 0);
            EXPECT_EQ(state.update(both_held), 0);
            // With Down, ScreenshotPoll consumes both delayed clicks.
            // Without Down it publishes them; neither may turn a page.
            HomeShoulderInput first_release;
            first_release.left_pressed = !screenshot && left_first;
            first_release.right_pressed = !screenshot && !left_first;
            first_release.left_held = !left_first;
            first_release.right_held = left_first;
            EXPECT_EQ(state.update(first_release), 0);
            HomeShoulderInput last_release;
            last_release.left_pressed = !screenshot && !left_first;
            last_release.right_pressed = !screenshot && left_first;
            EXPECT_EQ(state.update(last_release), 0);
            HomeShoulderInput next_click;
            next_click.right_pressed = true;
            EXPECT_EQ(state.update(next_click), 1);
        }
    }
    HomeShoulderPages together;
    EXPECT_EQ(together.update(both_held), 0);
    HomeShoulderInput both_released;
    both_released.left_pressed = both_released.right_pressed = true;
    EXPECT_EQ(together.update(both_released), 0);
}

static void test_classic_clicks_and_controllers() {
    HomeShoulderPages classic, other;
    HomeShoulderInput left_click;
    left_click.left_pressed = left_click.left_held = true;
    EXPECT_EQ(classic.update(left_click), -1);
    HomeShoulderInput left_held;
    left_held.left_held = true;
    EXPECT_EQ(classic.update(left_held), 0);
    EXPECT_EQ(classic.update({}), 0);
    HomeShoulderInput right_click;
    right_click.right_pressed = right_click.right_held = true;
    EXPECT_EQ(classic.update(right_click), 1);
    EXPECT_EQ(classic.update({}), 0);
    HomeShoulderInput chord;
    chord.left_pressed = chord.right_pressed = true;
    chord.left_held = chord.right_held = true;
    EXPECT_EQ(classic.update(chord), 0);
    EXPECT_EQ(other.update(left_click), -1);
    EXPECT_EQ(classic.update({}), 0);
}

int main() {
    test_gamecube_clicks();
    test_chord_releases();
    test_classic_clicks_and_controllers();
    if (g_failures != 0) return 1;
    std::cout << "ALL HOMEINPUT TESTS PASSED" << std::endl;
    return 0;
}
