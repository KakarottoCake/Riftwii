// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The real light controller, with libogc's alarms and GPIO register mocked.
#include "disclight.hpp"

#include <gccore.h>
#include <ogc/machine/processor.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include "loadersettings.hpp"

namespace {

int failures = 0;
#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << "Failed: " #condition " at " << __LINE__ << '\n'; \
            ++failures; \
        } \
    } while (0)

constexpr u32 kOtherPins = 0xA50118;  // sensor bar and unrelated outputs
u32 gpio = kOtherPins;
u32 interrupt_level = 0;
bool create_fails = false;
bool schedule_fails = false;
unsigned creates = 0;
unsigned schedules = 0;
bool timer_running = false;
alarmcallback pulse = nullptr;
sys_resetinfo* shutdown_hook = nullptr;
riftwii::LoaderSettings settings;

bool Lit() { return (gpio & 0x20) != 0; }

void Tick() {
    CHECK(timer_running);
    const u32 level = LockInterrupts();
    pulse(17, nullptr);
    RestoreInterrupts(level);
    CHECK((gpio & ~0x20u) == kOtherPins);
}

bool FailedOperation() {
    riftwii::wii::DiscLightActivity activity;
    CHECK(Lit());
    return false;
}

}  // namespace

u32 LockInterrupts() {
    const u32 previous = interrupt_level;
    interrupt_level = 1;
    return previous;
}

void RestoreInterrupts(u32 level) { interrupt_level = level; }

void mask32(u32 address, u32 clear, u32 set) {
    CHECK(interrupt_level != 0);
    CHECK(address == 0x0D8000C0);
    CHECK(clear == 0x20);
    CHECK(set == 0 || set == 0x20);
    gpio = (gpio & ~clear) | set;
}

s32 SYS_CreateAlarm(syswd_t* alarm) {
    ++creates;
    *alarm = 17;
    return create_fails ? -1 : 0;
}

s32 SYS_SetPeriodicAlarm(syswd_t alarm, const timespec* start, const timespec* period,
                         alarmcallback callback, void* argument) {
    CHECK(alarm == 17);
    CHECK(start->tv_sec == 0 && start->tv_nsec == 600000000);
    CHECK(period->tv_sec == 0 && period->tv_nsec == 600000000);
    CHECK(argument == nullptr);
    ++schedules;
    pulse = callback;
    timer_running = !schedule_fails;
    return schedule_fails ? -1 : 0;
}

s32 SYS_CancelAlarm(syswd_t alarm) {
    CHECK(alarm == 17);
    timer_running = false;
    return 0;
}

void SYS_RegisterResetFunc(sys_resetinfo* info) {
    CHECK(shutdown_hook == nullptr);
    shutdown_hook = info;
}

namespace riftwii::wii {
LoaderSettings& Settings() { return settings; }
void logf(const char*, ...) {}
}  // namespace riftwii::wii

int main(int argc, char** argv) {
    using namespace riftwii::wii;
    const std::string mode = argc > 1 ? argv[1] : "normal";
    create_fails = mode == "create-failure";
    schedule_fails = mode == "schedule-failure";

    // Default, parser validation, persistence and reset-to-default behavior.
    CHECK(settings.disc_light);
    settings.parse("disc_light = off\n");
    CHECK(!settings.disc_light);
    settings.parse("disc_light = invalid\n");
    CHECK(!settings.disc_light);
    riftwii::LoaderSettings restored;
    restored.parse(settings.serialize());
    CHECK(!restored.disc_light);
    CHECK(riftwii::settings_after_reset(settings).disc_light);
    {
        DiscLightActivity disabled;
        CHECK(creates == 0 && schedules == 0);
        CHECK(!Lit());
    }
    settings.parse("disc_light = on\n");

    if (create_fails || schedule_fails) {
        DiscLightActivity failed;
        CHECK(!Lit() && !timer_running);
        CHECK(creates == 1);
        CHECK(schedules == (create_fails ? 0u : 1u));
    } else {
        {
            DiscLightActivity outer;
            CHECK(Lit() && timer_running);
            Tick();
            CHECK(!Lit());
            {
                DiscLightActivity inner;
                CHECK(schedules == 1);  // no new pulse for nested loading
                CHECK(!Lit());
                Tick();
                CHECK(Lit());
            }
            CHECK(Lit() && timer_running);
        }
        CHECK(!Lit() && !timer_running);
        CHECK(!FailedOperation());
        CHECK(!Lit() && !timer_running);
        {
            DiscLightActivity transferring;
            StopDiscLight();
            CHECK(!Lit() && !timer_running);
            // Even a queued callback must not relight after the handoff.
            const u32 level = LockInterrupts();
            pulse(17, nullptr);
            RestoreInterrupts(level);
            CHECK(!Lit());
            transferring.Restart();
            CHECK(Lit() && timer_running);
        }
        CHECK(!Lit() && !timer_running);
        {
            std::optional<DiscLightActivity> oldScope;
            oldScope.emplace();
            StopDiscLight();
            DiscLightActivity retry;
            oldScope.reset();
            CHECK(Lit() && timer_running);
        }
        CHECK(!Lit() && !timer_running);
        {
            DiscLightActivity poweringOff;
            CHECK(Lit());
            CHECK(shutdown_hook != nullptr);
            CHECK(shutdown_hook->func(0) == 1);
            CHECK(shutdown_hook->func(1) == 1);
            CHECK(!Lit() && !timer_running);
        }
        CHECK(creates == 1);  // reuse the single loader-owned alarm
    }
    CHECK(gpio == kOtherPins);
    CHECK(interrupt_level == 0);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
