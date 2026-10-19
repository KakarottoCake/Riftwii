// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "nanduse.hpp"

#include <gccore.h>
#include <ogc/isfs.h>
#include <ogc/mutex.h>

namespace riftwii::wii {

namespace {

mutex_t g_lock = LWP_MUTEX_NULL;
int g_users = 0;

// The first call comes from the main thread at start, before any other
// thread reads the NAND, so creating the lock here is safe.
struct Locked {
    Locked() {
        if (g_lock == LWP_MUTEX_NULL) LWP_MutexInit(&g_lock, false);
        LWP_MutexLock(g_lock);
    }
    ~Locked() { LWP_MutexUnlock(g_lock); }
};

}  // namespace

s32 NandOpen() {
    Locked lock;
    const s32 result = ISFS_Initialize();
    if (result >= 0) ++g_users;
    return result;
}

void NandClose() {
    Locked lock;
    if (g_users == 0) return;
    if (--g_users == 0) ISFS_Deinitialize();
}

void NandCloseAll() {
    Locked lock;
    g_users = 0;
    ISFS_Deinitialize();
}

}  // namespace riftwii::wii
