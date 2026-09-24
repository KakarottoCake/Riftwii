// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

#include "riftwii/symsearch.hpp"

// Where the Gecko code handler (vendor-gecko/) is called from in a game:
// the end of the SDK's video retrace handler, which runs once per frame.
// It is found by four instructions every SDK build of it contains (the
// same pattern other loaders use); the first `blr` after them becomes a
// branch to the handler's entry, whose own `blr` then returns to the
// retrace handler's caller.
namespace riftwii {

constexpr std::uint32_t kCodeHandlerAddress = 0x80001800;
constexpr std::uint32_t kCodeHandlerEntry = 0x800018A8;
constexpr std::uint32_t kCodeListAddress = 0x800022A8;
constexpr std::uint32_t kCodeListEnd = 0x80003000;

// The address of the `blr` to replace, or 0 when no text has the pattern.
std::uint32_t find_cheat_hook(const std::vector<CodeRange>& text);

// The `b` from `from` to `to`, or 0 when out of reach.
std::uint32_t encode_b(std::uint32_t from, std::uint32_t to);

}  // namespace riftwii
