// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "fault_hook.h"
#include "pad_hook.h"
#include "shot_hook.h"
#include "vsd_hook.h"

// Only sizes are read by EstimateExtras. Native headers avoid depending
// on the host's byte order; synthetic Zstandard headers give known sizes.
template <typename Header>
Header budget_blob(std::uint32_t size) {
    Header header{};
    header.size = size;
    return header;
}

inline const auto budget_fault = budget_blob<rt_fault_header>(2816);
inline const auto budget_pad = budget_blob<rt_pad_header>(8192);
inline const auto budget_shot = budget_blob<rt_shot_header>(4096);
inline const auto budget_vsd = budget_blob<vsd_header>(9216);
inline const auto* riftwii_fault_bin = reinterpret_cast<const std::uint8_t*>(&budget_fault);
inline const auto* riftwii_pad_bin = reinterpret_cast<const std::uint8_t*>(&budget_pad);
inline const auto* riftwii_shot_bin = reinterpret_cast<const std::uint8_t*>(&budget_shot);
inline const auto* riftwii_vsd_bin = reinterpret_cast<const std::uint8_t*>(&budget_vsd);

// Single-segment frames with four-byte content sizes: 96 KiB and 160 KiB.
inline const std::uint8_t riftwii_rt_zst[] = {0x28, 0xB5, 0x2F, 0xFD, 0xA0, 0, 0x80, 1, 0};
inline const std::uint8_t riftwii_rt_rvz_zst[] = {0x28, 0xB5, 0x2F, 0xFD, 0xA0, 0, 0x80, 2, 0};
inline const auto riftwii_rt_zst_size = sizeof(riftwii_rt_zst);
inline const auto riftwii_rt_rvz_zst_size = sizeof(riftwii_rt_rvz_zst);
