// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace riftwii {

using Sha256Digest = std::array<std::uint8_t, 32>;

// SHA-256 (FIPS 180-4), written from the standard: catches a plugin file
// that was cut short or damaged on the card before any of it runs. No
// allocation, so the crash-free paths of the plugin loader can use it.
class Sha256 {
public:
    Sha256();
    void update(const std::uint8_t* data, std::size_t length);
    Sha256Digest finish();

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[8];
    std::uint8_t buffer_[64];
    std::size_t used_ = 0;
    std::uint64_t total_ = 0;
};

Sha256Digest sha256(const std::uint8_t* data, std::size_t length);

}  // namespace riftwii
