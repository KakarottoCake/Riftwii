// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The time left on a long job, for a progress box: fed the share done and
// the time since the job began, it waits until there is something to go on
// (ten seconds and some progress), then follows the rate with a moving
// average so a slow or fast moment doesn't make the figure jump. The
// wording is coarse on purpose: a figure that changes every second reads as
// noise.

#include <cstdint>

namespace riftwii {

enum class TimeLeftWording {
    WorkingOut,      // "Working out the time left..."
    UnderAMinute,    // "Less than a minute left"
    AboutAMinute,    // "About a minute left"
    AboutMinutes,    // "About {1} minutes left" (minutes >= 2)
};

struct TimeLeftText {
    TimeLeftWording wording = TimeLeftWording::WorkingOut;
    std::uint32_t minutes = 0;  // for AboutMinutes
};

// Seconds left to the wording: under 45 s is under a minute, under 90 s about
// a minute, then whole minutes to the nearest.
inline TimeLeftText time_left_text(std::uint64_t seconds) {
    TimeLeftText text;
    if (seconds < 45) {
        text.wording = TimeLeftWording::UnderAMinute;
    } else if (seconds < 90) {
        text.wording = TimeLeftWording::AboutAMinute;
    } else {
        text.wording = TimeLeftWording::AboutMinutes;
        text.minutes = static_cast<std::uint32_t>((seconds + 30) / 60);
    }
    return text;
}

class TimeLeft {
public:
    static constexpr std::uint64_t kFirstEstimateMs = 10000;  // nothing before this
    static constexpr std::uint64_t kSampleMs = 1000;          // the rate is sampled at most once a second
    static constexpr double kWeight = 0.15;                   // a new sample's share of the average

    // `fraction` of the job done (0 to 1, never going back), `ms` since it
    // began.
    void update(double fraction, std::uint64_t ms) {
        if (fraction < 0) fraction = 0;
        if (fraction > 1) fraction = 1;
        if (fraction < fraction_) fraction = fraction_;
        fraction_ = fraction;
        ms_ = ms;
        if (ms < sample_ms_ + kSampleMs) return;
        const double rate = (fraction - sample_fraction_) / double(ms - sample_ms_);
        sample_fraction_ = fraction;
        sample_ms_ = ms;
        if (ms < kFirstEstimateMs || fraction <= 0) return;
        if (rate_ <= 0) {
            // The first estimate: the average over the whole job so far.
            rate_ = fraction / double(ms);
        } else {
            rate_ = kWeight * rate + (1 - kWeight) * rate_;
        }
    }

    // Whether there is an estimate yet.
    bool ready() const { return rate_ > 0 && fraction_ < 1; }

    // Seconds left at the smoothed rate; 0 until ready().
    std::uint64_t seconds() const {
        if (!ready()) return 0;
        return static_cast<std::uint64_t>((1 - fraction_) / rate_ / 1000.0 + 0.5);
    }

    TimeLeftText text() const { return ready() ? time_left_text(seconds()) : TimeLeftText(); }

private:
    double fraction_ = 0;
    std::uint64_t ms_ = 0;
    double sample_fraction_ = 0;
    std::uint64_t sample_ms_ = 0;
    double rate_ = 0;  // share per millisecond
};

}  // namespace riftwii
