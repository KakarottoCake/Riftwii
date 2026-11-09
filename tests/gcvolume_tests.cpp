// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A GameCube game's Volume boost (runtime/gc/gc_volume.h): the gain each
// option gives and the scaling of the AI's buffers, as the blob runs it.
#include "gc_volume.h"

#include <cstdint>
#include <iostream>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

void test_gains() {
    EXPECT_EQ(gc_volume_gain(0), GC_VOLUME_UNITY);
    EXPECT_EQ(gc_volume_gain(3), GC_VOLUME_PLUS_3DB);
    EXPECT_EQ(gc_volume_gain(6), GC_VOLUME_PLUS_6DB);
    // Anything else (a damaged request the loader would refuse anyway) is
    // the game as made, never louder.
    EXPECT_EQ(gc_volume_gain(9), GC_VOLUME_UNITY);
    EXPECT_EQ(gc_volume_gain(0xFFFFFFFFu), GC_VOLUME_UNITY);
    // +3 dB is about 1.41 times, +6 dB about twice, and neither reaches
    // a doubling that would push half the range into the clamp.
    EXPECT_TRUE(GC_VOLUME_PLUS_3DB * 1000 / GC_VOLUME_UNITY >= 1410);
    EXPECT_TRUE(GC_VOLUME_PLUS_3DB * 1000 / GC_VOLUME_UNITY <= 1415);
    EXPECT_TRUE(GC_VOLUME_PLUS_6DB * 1000 / GC_VOLUME_UNITY >= 1990);
    EXPECT_TRUE(GC_VOLUME_PLUS_6DB < 2 * GC_VOLUME_UNITY);
}

void test_scale() {
    {
        // Quiet samples grow by the gain, both signs alike.
        std::vector<std::int16_t> samples = {0, 100, -100, 1000, -1000};
        gc_volume_scale(samples.data(), static_cast<std::uint32_t>(samples.size()), GC_VOLUME_PLUS_6DB);
        EXPECT_EQ(samples[0], 0);
        EXPECT_EQ(samples[1], 199);   // 100 * 511 / 256 = 199.6
        EXPECT_EQ(samples[2], -200);  // an arithmetic shift rounds down
        EXPECT_EQ(samples[3], 1996);
        EXPECT_EQ(samples[4], -1997);
    }
    {
        // Loud samples flatten at the ends of the range instead of
        // wrapping round to the other sign.
        std::vector<std::int16_t> samples = {32767, -32768, 20000, -20000, 16400};
        gc_volume_scale(samples.data(), static_cast<std::uint32_t>(samples.size()), GC_VOLUME_PLUS_6DB);
        EXPECT_EQ(samples[0], 32767);
        EXPECT_EQ(samples[1], -32768);
        EXPECT_EQ(samples[2], 32767);
        EXPECT_EQ(samples[3], -32768);
        EXPECT_EQ(samples[4], 32735);  // 16400 * 511 / 256, just inside
    }
    {
        // Unity leaves every sample as it was.
        std::vector<std::int16_t> samples = {32767, -32768, 1, -1, 12345};
        const std::vector<std::int16_t> before = samples;
        gc_volume_scale(samples.data(), static_cast<std::uint32_t>(samples.size()), GC_VOLUME_UNITY);
        EXPECT_TRUE(samples == before);
    }
    {
        // Only `count` samples: the next buffer's are not touched.
        std::vector<std::int16_t> samples = {1000, 1000, 1000};
        gc_volume_scale(samples.data(), 2, GC_VOLUME_PLUS_3DB);
        EXPECT_EQ(samples[0], 1414);  // 1000 * 362 / 256
        EXPECT_EQ(samples[1], 1414);
        EXPECT_EQ(samples[2], 1000);
    }
}

}  // namespace

int main() {
    test_gains();
    test_scale();
    if (g_failures == 0) std::cout << "gcvolume tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
