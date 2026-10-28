// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>

namespace riftwii::menumotion {

constexpr float kFrameMs = 1000.0f / 60.0f;

// A slow card read must not skip all the visible motion on the next draw.
inline float frame_ms(float elapsedMs, float slowMotion = 1.0f) {
    return std::clamp(elapsedMs, 0.0f, 50.0f) / std::max(1.0f, slowMotion);
}

// Exponential following keeps its current position when the target changes.
// The response is measured in milliseconds, independent of PAL/NTSC refresh.
inline float decay(float elapsedMs, float responseMs) {
    return std::exp(-std::max(0.0f, elapsedMs) / responseMs);
}

inline float follow(float current, float target, float elapsedMs, float responseMs, float snap = 0.001f) {
    const float next = target + (current - target) * decay(elapsedMs, responseMs);
    return std::fabs(target - next) <= snap ? target : next;
}

// Velocity is stored in pixels per nominal 60 Hz frame. Integrating the
// same decay used to slow it makes a flick travel equally far at 50 Hz.
inline float coast(float& velocity, float elapsedMs, float responseMs) {
    const float remaining = decay(elapsedMs, responseMs);
    const float distance = velocity * (responseMs / kFrameMs) * (1.0f - remaining);
    velocity *= remaining;
    return distance;
}

// Banners author their tracks at 60 frames per second. Keep fractional
// frames, and wrap only the loop so long sessions retain smooth timing.
inline float animation_frame(float frame, float elapsedMs, int startFrames, int loopFrames) {
    frame += std::max(0.0f, elapsedMs) / kFrameMs;
    const float start = static_cast<float>(std::max(0, startFrames));
    if (loopFrames > 0 && frame >= start) {
        return start + std::fmod(frame - start, static_cast<float>(loopFrames));
    }
    return std::min(frame, start);
}

inline float ease(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t < 0.5f) return 4 * t * t * t;
    const float remaining = -2 * t + 2;
    return 1 - remaining * remaining * remaining / 2;
}

inline float ease_out(float t) {
    const float remaining = 1 - std::clamp(t, 0.0f, 1.0f);
    return 1 - remaining * remaining * remaining;
}

inline float ease_back(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    if (t == 0 || t == 1) return t;
    constexpr float overshoot = 1.70158f;
    const float remaining = t - 1;
    return 1 + (overshoot + 1) * remaining * remaining * remaining + overshoot * remaining * remaining;
}

}  // namespace riftwii::menumotion
