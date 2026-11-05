// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "disclight.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>

#include "loadersettings.hpp"
#include "log.hpp"

namespace riftwii::wii {
namespace {

// WiiBrew documents Broadway's GPIO output register and SLOT_LED (bit 5,
// active high): https://wiibrew.org/wiki/Hollywood/GPIOs . Leave direction,
// ownership and every other pin alone, including the sensor bar and eject.
constexpr u32 kGpioOutput = 0x0D8000C0;
constexpr u32 kSlotLight = 0x20;
syswd_t g_alarm = 0;
bool g_alarmCreated = false;
bool g_shutdownRegistered = false;
unsigned g_users = 0;
unsigned g_generation = 0;
bool g_lit = false;
sys_resetinfo g_shutdown{};

// Callers hold the interrupt lock; the alarm already runs in that context.
void SetLight(bool lit) {
    mask32(kGpioOutput, kSlotLight, lit ? kSlotLight : 0);
    g_lit = lit;
}

void Pulse(syswd_t, void*) {
    if (g_users != 0) SetLight(!g_lit);
}

s32 OnShutdown(s32) {
    StopDiscLight();
    return 1;
}

}  // namespace

DiscLightActivity::DiscLightActivity() {
    Restart();
}

void DiscLightActivity::Restart() {
    if (!Settings().disc_light) return;

    u32 level;
    _CPU_ISR_Disable(level);
    if (active_ && generation_ == g_generation) {
        _CPU_ISR_Restore(level);
        return;
    }
    active_ = false;
    generation_ = g_generation;
    if (g_users != 0) {
        ++g_users;
        active_ = true;
        _CPU_ISR_Restore(level);
        return;
    }
    if (!g_shutdownRegistered) {
        g_shutdown.func = OnShutdown;
        g_shutdown.prio = 0;
        SYS_RegisterResetFunc(&g_shutdown);
        g_shutdownRegistered = true;
    }
    if (!g_alarmCreated) g_alarmCreated = SYS_CreateAlarm(&g_alarm) == 0;

    // A slow, even pulse continues through blocking reads and patching.
    // Failure to get a timer leaves the light off and the launch unchanged.
    const timespec interval{0, 600000000};
    if (g_alarmCreated && SYS_SetPeriodicAlarm(g_alarm, &interval, &interval, Pulse, nullptr) == 0) {
        g_users = 1;
        active_ = true;
        SetLight(true);
    } else {
        SetLight(false);
    }
    _CPU_ISR_Restore(level);
    if (!active_) logf("Disc slot light: the timer could not start; continuing with the light off.\n");
}

DiscLightActivity::~DiscLightActivity() {
    if (!active_) return;
    u32 level;
    _CPU_ISR_Disable(level);
    if (generation_ == g_generation && g_users != 0 && --g_users == 0) {
        SYS_CancelAlarm(g_alarm);
        SetLight(false);
    }
    _CPU_ISR_Restore(level);
}

void StopDiscLight() {
    u32 level;
    _CPU_ISR_Disable(level);
    // Scopes from before a handoff must not stop a later retry's light.
    ++g_generation;
    g_users = 0;
    if (g_alarmCreated) SYS_CancelAlarm(g_alarm);
    SetLight(false);
    _CPU_ISR_Restore(level);
}

}  // namespace riftwii::wii
