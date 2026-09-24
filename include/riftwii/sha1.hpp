// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace riftwii {

using Sha1Digest = std::array<std::uint8_t, 20>;

// SHA-1 (FIPS 180-4), for checking the hashes RVZ headers carry. Not for
// anything security related.
class Sha1 {
public:
    Sha1();
    void update(const std::uint8_t* data, std::size_t length);
    Sha1Digest finish();

private:
    void block(const std::uint8_t* p);
    std::uint32_t h_[5];
    std::uint8_t buffer_[64];
    std::size_t used_ = 0;
    std::uint64_t total_ = 0;
};

Sha1Digest sha1(const std::uint8_t* data, std::size_t length);

}
