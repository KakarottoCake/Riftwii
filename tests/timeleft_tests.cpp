// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The time-left estimate for progress boxes (riftwii/timeleft.hpp).
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <vector>

#include "riftwii/timeleft.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

// One stage of a replayed job: `seconds` long, moving the bar from `from` to
// `to` evenly (to == from: no progress at all, as a long write).
struct Stage {
    double seconds;
    double from, to;
};

// What a replay saw: how often the wording rose, by how many steps at most,
// and how many samples after the first minute were more than 30% off.
struct Replay {
    int rises = 0;
    std::uint32_t biggest_rise = 0;
    int off = 0;
    int samples = 0;
};

Replay replay(const std::vector<Stage>& stages, bool verbose = false) {
    double total = 0;
    for (const Stage& stage : stages) total += stage.seconds;
    TimeLeft t;
    Replay r;
    double clock = 0;
    bool have = false;
    std::uint32_t last_step = 0;
    for (const Stage& stage : stages) {
        for (double in = 0; in < stage.seconds; in += 0.25) {
            const double f = stage.from + (stage.to - stage.from) * in / stage.seconds;
            const std::uint64_t ms = static_cast<std::uint64_t>((clock + in) * 1000);
            t.update(f, ms);
            if (!t.ready()) continue;
            const std::uint32_t step = time_left_step(t.seconds());
            if (have && step > last_step) {
                ++r.rises;
                if (step - last_step > r.biggest_rise) r.biggest_rise = step - last_step;
            }
            have = true;
            last_step = step;
            const double truth = total - (clock + in);
            if (clock + in >= 60 && truth >= 10) {
                ++r.samples;
                const double shown = double(t.seconds());
                // Within 30% (or 15 s), or both under a minute (the wording
                // is the same).
                const bool close = (shown >= truth * 0.7 && shown <= truth * 1.3) || (truth < 45 && shown < 45) ||
                                   (shown > truth - 15 && shown < truth + 15);
                if (!close) ++r.off;
                if (verbose && static_cast<int>(clock + in) % 10 == 0 && in - static_cast<int>(in) < 0.01)
                    std::printf("%6.0f s  shown %4llu  truth %4.0f\n", clock + in, (unsigned long long)t.seconds(), truth);
            }
        }
        clock += stage.seconds;
    }
    return r;
}

// Jack's Wii (build 2611-186): the five files took 75, 88, 36, 98 and 52 s,
// and weigh 3.45 times their sections' size plus their unpacked size in the
// bar (ctgpupdate.cpp). Each file's bar moves 45% while
// its bases unpack, 35% while it is rebuilt and 20% while it is stored; the
// real time each stage takes is `split` (bases, rebuild, store), and in
// each second of storing, `silent` of it passes with no progress (the
// journal's sync before each 32 KiB is written).
std::vector<Stage> jacks_wii(double base, double rebuild, double store, double silent) {
    const double seconds[5] = {74.9, 88.3, 36.0, 98.2, 51.6};
    const double unpacked[5] = {4362592, 4430880, 2919648, 7710720, 3071776};
    const double section[5] = {2507836, 3498724, 1690508, 3104124, 1930136};
    double size[5];
    for (int i = 0; i < 5; ++i) size[i] = 3.45 * section[i] + unpacked[i];
    double all = 0;
    for (double s : size) all += s;
    std::vector<Stage> out;
    double at = 0;
    for (int i = 0; i < 5; ++i) {
        const double share = size[i] / all;
        const double a = at + share * 0.45, b = a + share * 0.35, c = at + share;
        out.push_back({seconds[i] * base, at, a});
        out.push_back({seconds[i] * rebuild, a, b});
        const double storing = seconds[i] * store;
        const int pieces = storing < 1 ? 1 : static_cast<int>(storing);
        double f = b;
        for (int k = 0; k < pieces; ++k) {
            const double next = b + (c - b) * (k + 1) / pieces;
            out.push_back({storing / pieces * (1 - silent), f, next});
            if (silent > 0) out.push_back({storing / pieces * silent, next, next});
            f = next;
        }
        at = c;
    }
    return out;
}

}  // namespace

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
    // A rate change: half as fast from 60 s on. The figure doesn't jump, and
    // follows within a few minutes.
    {
        TimeLeft t;
        double f = 0;
        std::uint64_t ms = 0;
        for (; ms <= 60000; ms += 250) {
            f = double(ms) / 300000.0;
            t.update(f, ms);
        }
        const std::uint64_t before = t.seconds();  // ~240 s
        EXPECT_TRUE(before >= 235 && before <= 245);
        for (int step = 0; step < 4; ++step) {
            ms += 250;
            f += 250.0 / 600000.0;
            t.update(f, ms);
        }
        EXPECT_TRUE(t.seconds() <= before);  // held, counting down
        for (int step = 0; step < 4 * 180; ++step) {  // three minutes later
            ms += 250;
            f += 250.0 / 600000.0;
            t.update(f, ms);
        }
        const double left = (1 - f) * 600.0;
        EXPECT_TRUE(double(t.seconds()) > left * 0.8 && double(t.seconds()) < left * 1.2);
    }
    // No progress for half a minute (a long write): the figure holds, neither
    // counting down past the work nor growing.
    {
        TimeLeft t;
        std::uint64_t ms = 0;
        double f = 0;
        for (; ms <= 60000; ms += 250) {
            f = double(ms) / 300000.0;
            t.update(f, ms);
        }
        std::uint64_t held = 0;
        for (int step = 0; step < 4 * 30; ++step) {
            ms += 250;
            t.update(f, ms);
            if (step == 4 * 5) held = t.seconds();
            if (step > 4 * 5) EXPECT_TRUE(t.seconds() == held);
        }
        EXPECT_TRUE(held >= 230 && held <= 245);
    }
    // A job that goes in fits and starts (four times as fast, then slow, ten
    // seconds each): never up by more than one step, and close after its
    // first minute.
    {
        std::vector<Stage> stages;
        double f = 0;
        for (int i = 0; i < 16; ++i) {
            const bool fast = i % 2 == 0;
            const double to = f + (fast ? 0.1 : 0.025);
            stages.push_back({10, f, to});
            f = to;
        }
        const Replay r = replay(stages);
        EXPECT_TRUE(r.biggest_rise <= 1);
        EXPECT_TRUE(r.off == 0);
    }
    // Jack's real Wii timings. Where the stages take about the shares the bar
    // gives them (within a tenth), the figure only ever falls or holds and is
    // close after the first minute.
    {
        const double splits[][4] = {
            {0.45, 0.35, 0.20, 0.5},   // as the bar weighs them, half of storing silent
            {0.40, 0.35, 0.25, 0.5},
            {0.50, 0.30, 0.20, 0.5},
            {0.45, 0.30, 0.25, 0.6},
            {0.35, 0.35, 0.30, 0.6},
            {0.50, 0.35, 0.15, 0.3},
        };
        for (const auto& split : splits) {
            const Replay r = replay(jacks_wii(split[0], split[1], split[2], split[3]));
            EXPECT_TRUE(r.samples > 0);
            EXPECT_TRUE(r.biggest_rise <= 1);
            EXPECT_TRUE(r.off == 0);
            if (r.off != 0 || r.biggest_rise > 1)
                std::printf("split %.2f/%.2f/%.2f silent %.1f: %d of %d off, rises %d (biggest %u)\n", split[0],
                            split[1], split[2], split[3], r.off, r.samples, r.rises, r.biggest_rise);
        }
    }
    // Stages far from their shares (as Dolphin's, or a card far slower):
    // the figure may be off for a while, but never rises more than a step.
    {
        const double splits[][4] = {
            {0.50, 0.45, 0.05, 0.0},
            {0.30, 0.30, 0.40, 0.5},
            {0.60, 0.25, 0.15, 0.3},
            {0.25, 0.50, 0.25, 0.8},
        };
        for (const auto& split : splits) {
            const Replay r = replay(jacks_wii(split[0], split[1], split[2], split[3]));
            EXPECT_TRUE(r.biggest_rise <= 1);
            EXPECT_TRUE(r.off * 4 < r.samples);  // close at least three quarters of the time
        }
    }
    // Done: no estimate any more.
    {
        TimeLeft t;
        t.update(0.5, 12000);
        EXPECT_TRUE(t.ready());
        t.update(1.0, 13000);
        EXPECT_TRUE(!t.ready());
    }
    // The wording's edges and steps.
    EXPECT_TRUE(time_left_text(0).wording == TimeLeftWording::UnderAMinute);
    EXPECT_TRUE(time_left_text(44).wording == TimeLeftWording::UnderAMinute);
    EXPECT_TRUE(time_left_text(45).wording == TimeLeftWording::AboutAMinute);
    EXPECT_TRUE(time_left_text(89).wording == TimeLeftWording::AboutAMinute);
    EXPECT_TRUE(time_left_text(90).wording == TimeLeftWording::AboutMinutes && time_left_text(90).minutes == 2);
    EXPECT_TRUE(time_left_text(149).minutes == 2);
    EXPECT_TRUE(time_left_text(150).minutes == 3);
    EXPECT_TRUE(time_left_text(3600).minutes == 60);
    EXPECT_TRUE(time_left_step(30) == 0 && time_left_step(60) == 1 && time_left_step(120) == 2 &&
                time_left_step(400) == 7);

    if (g_failures == 0) std::cout << "timeleft: all tests passed" << std::endl;
    return g_failures ? 1 : 0;
}
