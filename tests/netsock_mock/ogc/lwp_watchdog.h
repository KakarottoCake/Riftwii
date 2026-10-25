// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <network.h>

// One tick per millisecond; net_poll advances the fake clock.
extern u64 netsock_test_time;
inline u64 gettime() { return netsock_test_time; }
inline u64 diff_msec(u64 start, u64 end) { return end - start; }
inline u64 diff_ticks(u64 start, u64 end) { return end - start; }
inline u64 ticks_to_millisecs(u64 ticks) { return ticks; }
inline u64 millisecs_to_ticks(u64 milliseconds) { return milliseconds; }
