// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gccore.h>

// libogc's mutexes; a test defines these to count what is locked.
using mutex_t = u32;
#define LWP_MUTEX_NULL 0xffffffffu
s32 LWP_MutexInit(mutex_t* mutex, bool recursive);
s32 LWP_MutexLock(mutex_t mutex);
s32 LWP_MutexUnlock(mutex_t mutex);
