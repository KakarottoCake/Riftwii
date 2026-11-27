// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "fault_hook.h"
#include "pad_hook.h"
#include "shot_hook.h"
#include "vsd_hook.h"

// Only sizes are read by EstimateExtras: each set's big-endian size table
// (wii/core/packedblobs.cpp).

// The sizes the tests expect.
struct BudgetBlob {
    std::uint32_t size;
};
inline const BudgetBlob budget_fault{2816};
inline const BudgetBlob budget_pad{8192};

// The hooks' set: the virtual SD card (9216), screenshots (4096), the crash
// recorder (budget_fault).
inline const std::uint8_t riftwii_hooks_bin[] = {0, 0, 0x24, 0, 0, 0, 0x10, 0, 0, 0, 0x0B, 0};

// The pad builds' sizes (Makefile.runtime's riftwii_pad_sizes.h): the full
// one first, budget_pad's size.
#define RIFTWII_PAD_BUILD_BYTES {8192u, 8192u, 8192u, 8192u, 8192u, 8192u}

// The runtime builds' big-endian size table (wii/core/packedblobs.cpp):
// the plain build 96 KiB, the RVZ one 160 KiB.
inline const std::uint8_t riftwii_rts_bin[] = {0, 1, 0x80, 0, 0, 2, 0x80, 0};
