// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace riftwii::wii {

// Scoped loading/copying activity. Nested callers share one pulse, and the
// last return (success, cancellation or failure) leaves the light off. No
// pulse (and no GPIO write) while a cIOS is running or the setting is off.
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

// Called at the start of every IOS reload: the light goes dark now, since the
// IOS (and with it the pulse's context) is about to change. It comes back only
// through Restart() or a new scope, and only while the running IOS is not a
// cIOS (slot 200 or up); Restart() and the alarm both check, so a failed launch
// that leaves the menu on d2x never pulses.
void DiscLightIosReloaded();

}  // namespace riftwii::wii
