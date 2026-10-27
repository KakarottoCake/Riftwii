// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <gctypes.h>

// libogc's threads; tests/usbkeyboard_tests.cpp runs them on host threads.
using lwp_t = u32;
#define LWP_THREAD_NULL 0xffffffffu
s32 LWP_CreateThread(lwp_t* thread, void* (*entry)(void*), void* arg, void* stack, u32 stack_size, u8 priority);
s32 LWP_JoinThread(lwp_t thread, void** value);
s32 LWP_SuspendThread(lwp_t thread);
