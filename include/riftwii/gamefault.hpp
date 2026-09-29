// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// The crash record the fault blob writes when a game crashes
// (runtime/rtfault.h, big-endian), read back by the menu at its next
// start and written out as sd:/riftwii/gamecrash.txt for a problem
// report.
namespace riftwii {

// The record as text, or false with `error` when `bytes` is not one.
bool describe_game_fault(const std::uint8_t* bytes, std::size_t size, std::string& text, std::string& error);

}  // namespace riftwii
