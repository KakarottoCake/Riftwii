// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

// Scoped loading/copying activity. Nested callers share one pulse, and the
// last return (success, cancellation or failure) leaves the light off.
class DiscLightActivity {
public:
    DiscLightActivity();
    ~DiscLightActivity();
    DiscLightActivity(const DiscLightActivity&) = delete;
    DiscLightActivity& operator=(const DiscLightActivity&) = delete;

    // A system launch can return on failure after StopDiscLight. Resume
    // this operation's pulse while it tries the next way to start the game.
    void Restart();

private:
    bool active_ = false;
    unsigned generation_ = 0;
};

// Before handing memory and interrupts to another program. Also registered
// with libogc's shutdown hooks, so power-off/restart cannot leave a pulse.
void StopDiscLight();

}  // namespace riftwii::wii
