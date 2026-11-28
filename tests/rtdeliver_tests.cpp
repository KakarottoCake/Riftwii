// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Delivery (runtime/rtdeliver.h): what the game asked for, in its
// library's layout. Newly pressed is against what the game's KPAD read
// last got.
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "rtdeliver.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto expect_a_ = (a); const auto expect_b_ = (b); if (expect_a_ != expect_b_) { std::cerr << "FAILED: " #a " == " #b " (" << expect_a_ << " != " << expect_b_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

rtdeliver fresh() {
    rtdeliver d;
    std::memset(&d, 0, sizeof(d));
    return d;
}

// A press between two of the game's reads, taken at a WPADProbe the game
// never reads in between, still counts at its next read; a second read of
// the same sample sees the same words.
void TestWordsAgainstLastGot() {
    rtdeliver d = fresh();
    std::uint32_t trig = 0, release = 0;
    rtdeliver_take(&d, 0, 1, 0x8, &trig, &release);
    EXPECT_EQ(trig, 0x8u);
    rtdeliver_take(&d, 0, 1, 0x8, &trig, &release);  // same sample: same words
    EXPECT_EQ(trig, 0x8u);
    rtdeliver_take(&d, 0, 3, 0x8, &trig, &release);  // held on: not new
    EXPECT_EQ(trig, 0u);
    rtdeliver_take(&d, 0, 4, 0x0, &trig, &release);
    EXPECT_EQ(release, 0x8u);
    // Plugged in holding a button: not newly pressed.
    rtdeliver_arrived(&d, 1, 0x10);
    rtdeliver_take(&d, 1, 1, 0x10, &trig, &release);
    EXPECT_EQ(trig, 0u);
    rtdeliver_gone(&d, 1);
    rtdeliver_take(&d, 1, 2, 0x10, &trig, &release);
    EXPECT_EQ(trig, 0x10u);
    rtdeliver_take(&d, 7, 1, 0x10, &trig, &release);  // no such slot
    EXPECT_EQ(trig, 0u);
}

}  // namespace

int main() {
    TestWordsAgainstLastGot();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rtdeliver tests passed" << std::endl;
    return 0;
}
