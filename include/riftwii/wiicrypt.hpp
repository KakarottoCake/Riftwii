// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "riftwii/overlay.hpp"

// Reading an encrypted Wii partition (an ISO or WBFS game's), from the
// public pages https://wiibrew.org/wiki/Wii_disc and
// https://wiibrew.org/wiki/Ticket. The partition's ticket holds its title
// key, encrypted with the console's common key (AES-128-CBC, the title ID
// then eight zeros as the IV); the data is in 0x8000-byte clusters, each
// 0x400 bytes of hashes and 0x7C00 bytes of data, the data encrypted with
// the title key and the IV found at 0x3D0 of the (still encrypted) hash
// block. RiftWii never carries the common key: the menu reads it from the
// console's own OTP memory when it needs it (wii/otpkey.cpp).
namespace riftwii {

// AES-128, from FIPS-197.
class Aes128 {
public:
    explicit Aes128(const std::uint8_t key[16]);
    void encrypt_block(const std::uint8_t in[16], std::uint8_t out[16]) const;
    void decrypt_block(const std::uint8_t in[16], std::uint8_t out[16]) const;
    // CBC over whole blocks; `iv` is not changed. `in` and `out` may be the same.
    void cbc_decrypt(const std::uint8_t iv[16], const std::uint8_t* in, std::uint8_t* out, std::size_t length) const;
    void cbc_encrypt(const std::uint8_t iv[16], const std::uint8_t* in, std::uint8_t* out, std::size_t length) const;

private:
    std::uint8_t round_keys_[11][16];
};

constexpr std::size_t kTicketBytes = 0x2A4;
constexpr std::size_t kClusterBytes = 0x8000;
constexpr std::size_t kClusterDataBytes = 0x7C00;

// The title key from a partition's ticket. Only tickets for the ordinary
// common key (index 0) are taken: Korean games use another.
bool decrypt_title_key(const std::uint8_t* ticket, std::size_t size, const std::uint8_t common_key[16],
                       std::uint8_t title_key[16], std::string& error);

// One partition's data, decrypted and without the hash blocks: the same
// view RvzPartitionSource gives of an RVZ. Keeps the last cluster read.
class WiiPartitionSource final : public ByteSource {
public:
    WiiPartitionSource(const ByteSource& disc, std::uint64_t data_offset, std::uint64_t data_size,
                       const std::uint8_t title_key[16]);
    std::uint64_t size() const override { return size_; }
    bool read(std::uint64_t offset, std::uint8_t* destination, std::size_t length) const override;

private:
    const ByteSource& disc_;
    std::uint64_t data_offset_;
    std::uint64_t size_;
    Aes128 aes_;
    mutable std::uint64_t cached_ = UINT64_MAX;
    mutable std::vector<std::uint8_t> cluster_;
};

// The game partition of a raw disc (ISO, or WBFS through UsbDiscSource),
// opened with the title key from its ticket.
bool open_game_partition(const ByteSource& disc, const std::uint8_t common_key[16],
                         std::unique_ptr<WiiPartitionSource>& out, std::string& error);

}  // namespace riftwii
