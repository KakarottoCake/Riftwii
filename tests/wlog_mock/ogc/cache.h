// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
inline void DCFlushRange(void*, std::uint32_t) {}
inline void ICInvalidateRange(void*, std::uint32_t) {}
