// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <ctime>

using u32 = std::uint32_t;
using s32 = std::int32_t;
using syswd_t = u32;
using alarmcallback = void (*)(syswd_t, void*);
struct sys_resetinfo {
    s32 (*func)(s32) = nullptr;
    u32 prio = 0;
};

s32 SYS_CreateAlarm(syswd_t* alarm);
s32 SYS_SetPeriodicAlarm(syswd_t alarm, const timespec* start, const timespec* period,
                         alarmcallback callback, void* argument);
s32 SYS_CancelAlarm(syswd_t alarm);
void SYS_RegisterResetFunc(sys_resetinfo* info);
