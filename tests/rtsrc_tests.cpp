// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Sources (runtime/rtsrc.h): each player's controller, one sample per
// report. A controller plugged in, kept through a dropped report (a
// wireless pad's), and unplugged only after the grace period.
#include <cstdint>
#include <cstring>
#include <iostream>

extern "C" {
#include "rtsrc.h"
}

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { const auto expect_a_ = (a); const auto expect_b_ = (b); if (expect_a_ != expect_b_) { std::cerr << "FAILED: " #a " == " #b " (" << expect_a_ << " != " << expect_b_ << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kGrace = 300;

struct Pad {
    std::uint8_t b[RTSRC_PAD_BYTES] = {};
};

Pad report(std::uint32_t buttons) {
    Pad p;
    p.b[0] = static_cast<std::uint8_t>(buttons >> 8);
    p.b[1] = static_cast<std::uint8_t>(buttons);
    return p;
}

Pad none() {
    Pad p;
    p.b[10] = 0xFF;  // PAD_ERR_NO_CONTROLLER
    return p;
}

rtsrc fresh() {
    rtsrc s;
    std::memset(&s, 0, sizeof(s));
    return s;
}

void TestPlugReportUnplug() {
    rtsrc s = fresh();
    EXPECT_EQ(rtsrc_port(&s, 0, none().b, kGrace, 10), RTSRC_NOTHING);
    EXPECT_EQ(rtsrc_port(&s, 0, report(RTSRC_A).b, kGrace, 20), RTSRC_ARRIVED);
    EXPECT_EQ(rtsrc_port(&s, 0, report(RTSRC_A).b, kGrace, 30), RTSRC_REPORT);
    EXPECT_EQ(s.present, 1u);
    rtsrc_sample out;
    rtsrc_latest(&s, 0, &out);
    EXPECT_EQ(out.buttons, static_cast<std::uint32_t>(RTSRC_A));
    // A dropped report: the same sample again, still there.
    EXPECT_EQ(rtsrc_port(&s, 0, none().b, kGrace, 100), RTSRC_REPEAT);
    EXPECT_EQ(s.samples[0], 3u);
    // Nothing for the whole grace period: unplugged.
    EXPECT_EQ(rtsrc_port(&s, 0, none().b, kGrace, 30 + kGrace), RTSRC_GONE);
    EXPECT_EQ(s.present, 0u);
    rtsrc_latest(&s, 0, &out);  // unplugged: nothing held, not its last report
    EXPECT_EQ(out.buttons, 0u);
    EXPECT_EQ(rtsrc_port(&s, 0, none().b, kGrace, 1000), RTSRC_NOTHING);
    // Back: arrives afresh.
    EXPECT_EQ(rtsrc_port(&s, 0, report(0).b, kGrace, 1100), RTSRC_ARRIVED);
}

// The time base wraps (about every 70 s): a report just before the wrap
// and a dropped one just after are a few ticks apart, not a long gap.
void TestGraceAcrossWrap() {
    rtsrc s = fresh();
    rtsrc_port(&s, 2, report(0).b, kGrace, 0xFFFFFF00u);
    EXPECT_EQ(rtsrc_port(&s, 2, none().b, kGrace, 0x20u), RTSRC_REPEAT);
}

void TestSampleDecode() {
    Pad p = report(RTSRC_START | RTSRC_LEFT);
    p.b[6] = 200;
    p.b[7] = 10;
    rtsrc_sample out;
    rtsrc_decode(p.b, &out);
    EXPECT_EQ(out.buttons, static_cast<std::uint32_t>(RTSRC_START | RTSRC_LEFT));
    EXPECT_EQ(out.trigger[0], 200);
    EXPECT_EQ(out.trigger[1], 10);
    rtsrc s = fresh();
    rtsrc_latest(&s, 9, &out);  // no such port: nothing
    EXPECT_EQ(out.buttons, 0u);
    EXPECT_EQ(rtsrc_port(&s, 9, p.b, kGrace, 5), RTSRC_NOTHING);
}

}  // namespace

int main() {
    TestPlugReportUnplug();
    TestGraceAcrossWrap();
    TestSampleDecode();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "rtsrc tests passed" << std::endl;
    return 0;
}
