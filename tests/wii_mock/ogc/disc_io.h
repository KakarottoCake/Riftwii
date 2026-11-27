// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>

// libogc2's disc interface, as libfat sees a device: every callback takes the
// interface, and the members are const outside libogc.
using sec_t = std::uint64_t;
#define FEATURE_MEDIUM_CANREAD 0x00000001
#define FEATURE_MEDIUM_CANWRITE 0x00000002
#define FEATURE_MEDIUM_CANERASE 0x00000004
#define FEATURE_WII_SD 0x00001000
struct DISC_INTERFACE;
using FN_MEDIUM_STARTUP = bool (*)(DISC_INTERFACE*);
using FN_MEDIUM_ISINSERTED = bool (*)(DISC_INTERFACE*);
using FN_MEDIUM_READSECTORS = bool (*)(DISC_INTERFACE*, sec_t sector, sec_t count, void* buffer);
using FN_MEDIUM_WRITESECTORS = bool (*)(DISC_INTERFACE*, sec_t sector, sec_t count, const void* buffer);
using FN_MEDIUM_ERASESECTORS = bool (*)(DISC_INTERFACE*, sec_t sector, sec_t count);
using FN_MEDIUM_FLUSH = bool (*)(DISC_INTERFACE*);
using FN_MEDIUM_SHUTDOWN = bool (*)(DISC_INTERFACE*);
struct DISC_INTERFACE {
    const std::uint32_t ioType;
    const std::uint32_t features;
    const FN_MEDIUM_STARTUP startup;
    const FN_MEDIUM_ISINSERTED isInserted;
    const FN_MEDIUM_READSECTORS readSectors;
    const FN_MEDIUM_WRITESECTORS writeSectors;
    const FN_MEDIUM_ERASESECTORS eraseSectors;
    const FN_MEDIUM_FLUSH flush;
    const FN_MEDIUM_SHUTDOWN shutdown;
    const sec_t numberOfSectors;
    const std::uint32_t sectorsPerBlock;
    const std::uint32_t bytesPerSector;
};
