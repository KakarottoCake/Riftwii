// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

using lwp_t = unsigned;
constexpr lwp_t LWP_THREAD_NULL = 0;
lwp_t LWP_GetSelf();
int LWP_CreateThread(lwp_t*, void* (*)(void*), void*, void*, std::size_t, int);
int LWP_JoinThread(lwp_t, void**);
