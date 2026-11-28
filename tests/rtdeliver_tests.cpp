// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Delivery (runtime/rtdeliver.h): what the game asked for, in its
// library's layout. Newly pressed is against what the game's KPAD read
// last got; a raw report is as long as the data format the game asked for.
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "rtdeliver.h"
#include "rthome.h"
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

rtshape_out still() {
    rtshape_out f;
    std::memset(&f, 0, sizeof(f));
    rtshape_rest(&f);
    return f;
}

void TestReportLength() {
    rtshape_out f = still();
    f.hold = 0x0800;  // A
    std::uint8_t out[RTDELIVER_WPAD_FS_BYTES + 4];
    std::memset(out, 0xEE, sizeof(out));
    EXPECT_EQ(rtdeliver_wpad(&f, 1, 0, 1, out), RTDELIVER_WPAD_CORE_BYTES);
    EXPECT_EQ(out[0], 0x08);
    EXPECT_EQ(out[40], 1);                        // a Wii Remote with a Nunchuk
    EXPECT_EQ(out[RTDELIVER_WPAD_CORE_BYTES], 0xEE);  // nothing past the format's report
    EXPECT_EQ(rtdeliver_wpad(&f, 4, 0, 1, out), RTDELIVER_WPAD_FS_BYTES);
    // The Nunchuk at rest: 1 g along -Y is +1 g on WPAD's z (out[46..47]).
    EXPECT_EQ(static_cast<std::int16_t>((out[46] << 8) | out[47]), RTDELIVER_ACC_G);
}

// #616 (CodeRabbit): the sideways Wii Remote told a Nunchuk format worked
// gets a report that long, the Nunchuk's part zeros, the device a bare Wii
// Remote.
void TestSidewaysNunchukFormat() {
    rtshape_out f = still();
    std::uint8_t out[RTDELIVER_WPAD_FS_BYTES];
    std::memset(out, 0xEE, sizeof(out));
    EXPECT_EQ(rtdeliver_wpad(&f, 5, 1, 0, out), RTDELIVER_WPAD_FS_BYTES);
    EXPECT_EQ(out[40], 0);
    for (std::uint32_t i = 42; i < RTDELIVER_WPAD_FS_BYTES; ++i) EXPECT_EQ(out[i], 0);
    // Camera on, pointer hidden: two dots of size 0.
    EXPECT_EQ(out[13], 0);
    EXPECT_TRUE(rtdeliver_format_has_camera(5));
    EXPECT_TRUE(!rtdeliver_format_has_camera(4));
}

void TestDotsFollowPointer() {
    rtshape_out f = still();
    f.pointer_shown = 1;
    std::uint8_t out[RTDELIVER_WPAD_CORE_BYTES];
    rtdeliver_wpad(&f, 2, 1, 0, out);
    const int x0 = (out[8] << 8) | out[9];
    f.pointer[0] = RTHOME_POINTER_FULL / 2;  // pointing right
    rtdeliver_wpad(&f, 2, 1, 0, out);
    const int x1 = (out[8] << 8) | out[9];
    EXPECT_TRUE(x1 < x0);  // the dots move against the pointer
    EXPECT_EQ(out[13], RTDELIVER_DOT_SIZE);
}

}  // namespace

int main() {
    TestWordsAgainstLastGot();
    TestReportLength();
    TestSidewaysNunchukFormat();
    TestDotsFollowPointer();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rtdeliver tests passed" << std::endl;
    return 0;
}
