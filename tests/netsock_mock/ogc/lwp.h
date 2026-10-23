// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

using lwp_t = unsigned;
constexpr lwp_t LWP_THREAD_NULL = 0;
inline lwp_t LWP_GetSelf() { return 1; }
inline int LWP_CreateThread(lwp_t*, void* (*)(void*), void*, void*, std::size_t, int) { return -1; }
inline int LWP_JoinThread(lwp_t, void**) { return 0; }
