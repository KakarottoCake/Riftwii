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
    static constexpr std::uint64_t kSampleMs = 1000;          // one sample a second
    static constexpr int kWindow = 30;                        // the rate over the last 30 samples
    static constexpr double kKeep = 0.2;                      // the shown figure holds within 20%...
    static constexpr std::uint64_t kSustainMs = 10000;        // ...or for 10 s outside it

    // `fraction` of the job done (0 to 1, never going back), `ms` since it
    // began.
    void update(double fraction, std::uint64_t ms) {
        if (fraction < 0) fraction = 0;
        if (fraction > 1) fraction = 1;
        if (fraction < fraction_) fraction = fraction_;
        fraction_ = fraction;
        if (count_ > 0 && ms < sample_ms_[(next_ + kWindow - 1) % kWindow] + kSampleMs) return;
        // The rate over the window: a stage that is quick or slow for a few
        // seconds (files differ) moves it little; a real change shows within
        // half a minute.
        const int oldest = count_ < kWindow ? 0 : next_;
        const double from_f = count_ == 0 ? 0 : sample_fraction_[oldest];
        const std::uint64_t from_ms = count_ == 0 ? 0 : sample_ms_[oldest];
        sample_fraction_[next_] = fraction;
        sample_ms_[next_] = ms;
        next_ = (next_ + 1) % kWindow;
        if (count_ < kWindow) ++count_;
        if (ms < kFirstEstimateMs || fraction <= 0 || ms <= from_ms) return;
        const double rate = count_ < kWindow ? fraction / double(ms) : (fraction - from_f) / double(ms - from_ms);
        if (rate <= 0) return;
        rate_ = rate;
        // What is shown counts down by itself; only an estimate that stays
        // more than kKeep away from it for kSustainMs moves it.
        const double estimate = (1 - fraction) / rate_;  // ms
        const double counted = shown_ms_ > double(ms - shown_at_) ? shown_ms_ - double(ms - shown_at_) : 0;
        const bool outside = estimate < counted * (1 - kKeep) || estimate > counted * (1 + kKeep);
        if (!outside) outside_since_ = 0;
        else if (outside_since_ == 0) outside_since_ = ms;
        if (shown_at_ == 0 || (outside && ms - outside_since_ >= kSustainMs)) {
            shown_ms_ = estimate;
            outside_since_ = 0;
        } else {
            shown_ms_ = counted;
        }
        shown_at_ = ms;
    }

    // Whether there is an estimate yet.
    bool ready() const { return rate_ > 0 && fraction_ < 1; }

    // Seconds left as shown; 0 until ready().
    std::uint64_t seconds() const {
        if (!ready()) return 0;
        return static_cast<std::uint64_t>(shown_ms_ / 1000.0 + 0.5);
    }

    // Seconds left at the window's rate alone, without the hold.
    std::uint64_t raw_seconds() const {
        if (!ready()) return 0;
        return static_cast<std::uint64_t>((1 - fraction_) / rate_ / 1000.0 + 0.5);
    }

    TimeLeftText text() const { return ready() ? time_left_text(seconds()) : TimeLeftText(); }

private:
    double fraction_ = 0;
    double sample_fraction_[kWindow] = {};
    std::uint64_t sample_ms_[kWindow] = {};
    int next_ = 0;
    int count_ = 0;
    double rate_ = 0;  // share per millisecond
    double shown_ms_ = 0;
    std::uint64_t shown_at_ = 0;
    std::uint64_t outside_since_ = 0;
};

}  // namespace riftwii
