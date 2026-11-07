// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "rtfat.h"

// Low-memory fixtures reserve two sectors immediately after the operation
// (or the enclosing request). Classification deliberately leaves them unbound.
inline void BindFatTestBuffers(rtfat_op& op) {
#ifndef RTFAT_REFERENCE
    const auto end = reinterpret_cast<std::uintptr_t>(&op) + sizeof(op);
    const auto aligned = (end + 31) & ~std::uintptr_t(31);
    op.fat_sector = reinterpret_cast<std::uint8_t*>(aligned);
    op.sector = op.fat_sector + RTFAT_SECTOR_BYTES;
#else
    (void)op;
#endif
}
