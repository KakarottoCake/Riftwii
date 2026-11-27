// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
// Synthetic headers let the real planner/install code run on the host.
// Real PowerPC images are checked separately by Makefile.runtime.
alignas(32) inline unsigned char riftwii_wlog_bin[512]{};
alignas(32) inline unsigned char riftwii_wlog_trace_bin[480]{};
alignas(32) inline unsigned char riftwii_wlog_translate_bin[448]{};
inline constexpr std::uint32_t riftwii_wlog_bin_size = sizeof(riftwii_wlog_bin);
inline constexpr std::uint32_t riftwii_wlog_trace_bin_size = sizeof(riftwii_wlog_trace_bin);
inline constexpr std::uint32_t riftwii_wlog_translate_bin_size = sizeof(riftwii_wlog_translate_bin);
