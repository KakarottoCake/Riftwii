// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <ogc/lwp_watchdog.h>

inline int usleep(unsigned microseconds) {
    netsock_test_time += microseconds / 1000;
    return 0;
}
