// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The time left on a long job, for a progress box: fed the share done and
// the time since the job began, it waits until there is something to go on
// (ten seconds and some progress), then estimates from the rate over the
// last two minutes (the whole job until then), which a stage quicker or
// slower than its share of the bar (a Wii's SD card is slower than
// Dolphin's) hardly moves, while a real change still shows. The
// figure shown counts down by itself and only follows the estimate when it
// stays well away for a while; it never rises more than one step of the
// wording at a time, never jumps more than twice or half, and holds still
// while no progress comes in. The wording is coarse on purpose.

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

// The wording's steps in order: 0 under a minute, 1 about a minute, then N
// for about N minutes.
inline std::uint32_t time_left_step(std::uint64_t seconds) {
    const TimeLeftText text = time_left_text(seconds);
    if (text.wording == TimeLeftWording::UnderAMinute) return 0;
    if (text.wording == TimeLeftWording::AboutAMinute) return 1;
    return text.minutes;
}

// The middle of a step, in seconds.
inline double time_left_step_seconds(std::uint32_t step) {
    if (step == 0) return 30;
    if (step == 1) return 67;
    return 60.0 * step;
}

class TimeLeft {
public:
    static constexpr std::uint64_t kFirstEstimateMs = 10000;  // nothing before this
    static constexpr std::uint64_t kSampleMs = 1000;          // looked at once a second
    static constexpr std::uint64_t kStallMs = 4000;           // no progress this long: hold
    static constexpr int kWindow = 120;                       // the rate over the last two minutes
    static constexpr double kKeep = 0.1;                      // the shown figure holds within 10%...
    static constexpr std::uint64_t kSustainMs = 8000;         // ...or for 8 s outside it

    // `fraction` of the job done (0 to 1, never going back), `ms` since it
    // began.
    void update(double fraction, std::uint64_t ms) {
        if (fraction < 0) fraction = 0;
        if (fraction > 1) fraction = 1;
        if (fraction > fraction_) progress_ms_ = ms;
        if (fraction < fraction_) fraction = fraction_;
        fraction_ = fraction;
        if (sampled_ && ms < sample_ms_ + kSampleMs) return;
        sampled_ = true;
        sample_ms_ = ms;
        // The oldest sample in the window (the start, until it is full).
        const bool full = count_ == kWindow;
        const double from_f = full ? window_f_[next_] : 0;
        const std::uint64_t from_ms = full ? window_ms_[next_] : 0;
        window_f_[next_] = fraction;
        window_ms_[next_] = ms;
        next_ = (next_ + 1) % kWindow;
        if (count_ < kWindow) ++count_;
        if (ms < kFirstEstimateMs || fraction <= 0 || fraction <= from_f || ms <= from_ms) return;
        const double estimate = (1 - fraction) * double(ms - from_ms) / (fraction - from_f);  // ms
        if (shown_at_ == 0) {
            shown_ms_ = estimate;
            shown_at_ = ms;
            return;
        }
        const double elapsed = double(ms - shown_at_);
        shown_at_ = ms;
        if (ms - progress_ms_ >= kStallMs) {
            // Nothing came in for a while (a long write): the figure holds
            // rather than counting down past the work or growing without end.
            outside_since_ = 0;
            return;
        }
        const double counted = shown_ms_ > elapsed ? shown_ms_ - elapsed : 0;
        const bool outside = estimate < counted * (1 - kKeep) || estimate > counted * (1 + kKeep);
        if (!outside) outside_since_ = 0;
        else if (outside_since_ == 0) outside_since_ = ms;
        shown_ms_ = counted;
        if (!outside || ms - outside_since_ < kSustainMs) return;
        outside_since_ = 0;
        // Follow it, at most twice or half the figure, and up by one step of
        // the wording at most.
        double next = estimate;
        if (counted > 0) {
            if (next > counted * 2) next = counted * 2;
            if (next < counted / 2) next = counted / 2;
        }
        const std::uint32_t from = time_left_step(static_cast<std::uint64_t>(counted / 1000.0 + 0.5));
        const std::uint32_t to = time_left_step(static_cast<std::uint64_t>(next / 1000.0 + 0.5));
        if (to > from + 1) next = time_left_step_seconds(from + 1) * 1000.0;
        shown_ms_ = next;
    }

    // Whether there is an estimate yet.
    bool ready() const { return shown_at_ != 0 && fraction_ < 1; }

    // Seconds left as shown; 0 until ready().
    std::uint64_t seconds() const {
        if (!ready()) return 0;
        return static_cast<std::uint64_t>(shown_ms_ / 1000.0 + 0.5);
    }

    TimeLeftText text() const { return ready() ? time_left_text(seconds()) : TimeLeftText(); }

private:
    double fraction_ = 0;
    bool sampled_ = false;
    std::uint64_t sample_ms_ = 0;
    std::uint64_t progress_ms_ = 0;
    double shown_ms_ = 0;
    std::uint64_t shown_at_ = 0;
    std::uint64_t outside_since_ = 0;
    double window_f_[kWindow] = {};
    std::uint64_t window_ms_[kWindow] = {};
    int next_ = 0;
    int count_ = 0;
};

}  // namespace riftwii
