// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// libogc's disc interface, as libfat sees a device.
using sec_t = std::uint32_t;
#define FEATURE_MEDIUM_CANREAD 0x00000001
#define FEATURE_MEDIUM_CANWRITE 0x00000002
#define FEATURE_WII_SD 0x00000100
struct DISC_INTERFACE {
    unsigned long ioType;
    unsigned long features;
    bool (*startup)();
    bool (*isInserted)();
    bool (*readSectors)(sec_t sector, sec_t count, void* buffer);
    bool (*writeSectors)(sec_t sector, sec_t count, const void* buffer);
    bool (*clearStatus)();
    bool (*shutdown)();
};
