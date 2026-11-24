// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The few float functions the canvas (src/art/canvas.cpp) and the store's
// icons (src/art/storeicons.cpp) call. A plugin links no C library, and
// these only shape icons a few dozen pixels across, so they are short and
// accurate to well under a pixel rather than to the last bit.

#include <cstdint>

extern "C" {

float floorf(float x) {
    // Pixel coordinates: far inside an int's range.
    const float whole = static_cast<float>(static_cast<std::int32_t>(x));
    return whole > x ? whole - 1.0f : whole;
}

float ceilf(float x) {
    const float whole = static_cast<float>(static_cast<std::int32_t>(x));
    return whole < x ? whole + 1.0f : whole;
}

float sqrtf(float x) {
    if (x <= 0.0f) return 0.0f;
    // A first guess from the float's exponent (halved), then Newton's steps.
    union {
        float f;
        std::uint32_t u;
    } guess{x};
    guess.u = (guess.u >> 1) + 0x1FC00000u;
    float r = guess.f;
    for (int i = 0; i < 4; ++i) r = 0.5f * (r + x / r);
    return r;
}

float hypotf(float x, float y) { return sqrtf(x * x + y * y); }

float sinf(float x) {
    constexpr float kPi = 3.14159265f, kTwoPi = 6.28318531f;
    // Into -pi..pi, then into -pi/2..pi/2, where the series is close.
    x -= kTwoPi * floorf((x + kPi) / kTwoPi);
    if (x > kPi / 2) x = kPi - x;
    if (x < -kPi / 2) x = -kPi - x;
    const float x2 = x * x;
    return x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
}

float cosf(float x) { return sinf(x + 1.57079633f); }

// What the compiler makes of a sin and a cos of the same angle.
void sincosf(float x, float* s, float* c) {
    *s = sinf(x);
    *c = cosf(x);
}

}  // extern "C"
