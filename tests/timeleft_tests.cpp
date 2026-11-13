// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The time-left estimate for progress boxes (riftwii/timeleft.hpp).
#include <cstdint>
#include <iostream>

#include "riftwii/timeleft.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

int main() {
    // Nothing before ten seconds, however fast it goes.
    {
        TimeLeft t;
        for (std::uint64_t ms = 0; ms < 10000; ms += 200) {
            t.update(double(ms) / 20000.0, ms);
            EXPECT_TRUE(!t.ready());
            EXPECT_TRUE(t.text().wording == TimeLeftWording::WorkingOut);
        }
        t.update(0.5, 10000);
        EXPECT_TRUE(t.ready());
    }
    // Nor without progress.
    {
        TimeLeft t;
        for (std::uint64_t ms = 0; ms <= 30000; ms += 500) t.update(0, ms);
        EXPECT_TRUE(!t.ready());
    }
    // A steady rate: a 300 s job reads its true time left all the way.
    {
        TimeLeft t;
        for (std::uint64_t ms = 0; ms <= 240000; ms += 250) {
            t.update(double(ms) / 300000.0, ms);
            if (ms >= 10000) {
                const std::uint64_t truth = (300000 - ms) / 1000;
                const std::uint64_t got = t.seconds();
                EXPECT_TRUE(got + 2 >= truth && got <= truth + 2);
            }
        }
        EXPECT_TRUE(t.text().wording == TimeLeftWording::AboutAMinute);  // 60 s left
    }
    // A rate change: half as fast from 60 s on. The figure shown doesn't
    // jump at once; within a couple of minutes it follows the new rate.
    {
        TimeLeft t;
        double f = 0;
        std::uint64_t ms = 0;
        for (; ms <= 60000; ms += 250) {
            f = double(ms) / 300000.0;  // 0.2 at 60 s
            t.update(f, ms);
        }
        const std::uint64_t before = t.seconds();  // ~240 s
        EXPECT_TRUE(before >= 235 && before <= 245);
        for (int step = 0; step < 4; ++step) {  // one more second at half the rate
            ms += 250;
            f += 250.0 / 600000.0;
            t.update(f, ms);
        }
        EXPECT_TRUE(t.seconds() <= before);  // held, counting down
        for (int step = 0; step < 4 * 150; ++step) {  // two and a half minutes later
            ms += 250;
            f += 250.0 / 600000.0;
            t.update(f, ms);
        }
        const double left = (1 - f) * 600.0;  // the true time left at the new rate, in s
        EXPECT_TRUE(double(t.seconds()) > left * 0.9 && double(t.seconds()) < left * 1.1);
    }
    // A job that goes in fits and starts (twice as fast, then half as fast,
    // ten seconds each, as files' stages do): after its first minute the
    // wording only ever goes down, and it stays within a quarter of the
    // truth.
    {
        TimeLeft t;
        double f = 0;
        int ups = 0, off = 0;
        TimeLeftText last;
        for (std::uint64_t ms = 0; ms <= 400000 && f < 1; ms += 250) {
            const bool fast = (ms / 10000) % 2 == 0;
            f += fast ? 250.0 / 200000.0 : 250.0 / 800000.0;
            t.update(f, ms);
            const TimeLeftText now = t.text();
            if (ms > 60000 && f < 0.95) {
                const double truth = (1 - f) * 320.0;  // at the average rate, in s
                if (double(t.seconds()) < truth * 0.75 || double(t.seconds()) > truth * 1.25) ++off;
            }
            if (ms > 60000 && last.wording != TimeLeftWording::WorkingOut &&
                now.wording != TimeLeftWording::WorkingOut) {
                const bool up = int(now.wording) > int(last.wording) ||
                                (now.wording == last.wording && now.minutes > last.minutes);
                if (up) ++ups;
            }
            last = now;
        }
        EXPECT_TRUE(ups == 0);
        EXPECT_TRUE(off == 0);
    }
    // Done: no estimate any more.
    {
        TimeLeft t;
        t.update(0.5, 12000);
        EXPECT_TRUE(t.ready());
        t.update(1.0, 13000);
        EXPECT_TRUE(!t.ready());
    }
    // The wording's edges.
    EXPECT_TRUE(time_left_text(0).wording == TimeLeftWording::UnderAMinute);
    EXPECT_TRUE(time_left_text(44).wording == TimeLeftWording::UnderAMinute);
    EXPECT_TRUE(time_left_text(45).wording == TimeLeftWording::AboutAMinute);
    EXPECT_TRUE(time_left_text(89).wording == TimeLeftWording::AboutAMinute);
    EXPECT_TRUE(time_left_text(90).wording == TimeLeftWording::AboutMinutes && time_left_text(90).minutes == 2);
    EXPECT_TRUE(time_left_text(149).minutes == 2);
    EXPECT_TRUE(time_left_text(150).minutes == 3);
    EXPECT_TRUE(time_left_text(3600).minutes == 60);

    if (g_failures == 0) std::cout << "timeleft: all tests passed" << std::endl;
    return g_failures ? 1 : 0;
}
