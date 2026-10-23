// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <ogc/disc_io.h>

// libogc's own SD driver (/dev/sdio/slot0); a test defines it.
#define DEVICE_TYPE_WII_SD (('W' << 24) | ('I' << 16) | ('S' << 8) | 'D')
extern const DISC_INTERFACE __io_wiisd;
