// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "riftwii/fat32.hpp"

// "efa:", CTGP-R's package disk: sd:/ctgpr/blob.bin, a FAT32 image encrypted
// with AES-128-CBC in independent 32 KiB chunks (docs/CTGPR.md). This
// reads it without decrypting it to disk: a BlockReader that decrypts the
// chunk holding the requested sector, which the existing Fat32Volume can
// mount. The key is a runtime input (a file on the card), not part of the
// source.
namespace riftwii {

// AES-128, decryption only.
class Aes128Decrypt {
public:
    explicit Aes128Decrypt(const std::array<std::uint8_t, 16>& key);
    void decrypt_block(const std::uint8_t in[16], std::uint8_t out[16]) const;
    // CBC over `length` bytes (a multiple of 16), in place allowed.
    void decrypt_cbc(const std::uint8_t iv[16], const std::uint8_t* in, std::uint8_t* out, std::size_t length) const;

private:
    std::uint8_t round_keys_[11][16];
};

constexpr std::uint32_t kCtgpEfaChunkSectors = 64;  // 32 KiB
constexpr std::uint32_t kCtgpEfaChunkBytes = 64 * 512;

// Reads `length` bytes of the encrypted file at `offset`.
using ByteReader = std::function<bool(std::uint64_t offset, std::uint32_t length, std::uint8_t* out)>;

// The fixed words of a chunk's IV (big endian words 0, 1 and 3; word 2 is
// the chunk's first sector). Their values are CTGP-R's, read at run time
// from tables.bin (riftwii/ctgptables.hpp).
struct CtgpChunkIv {
    std::uint32_t w0 = 0, w1 = 0, w3 = 0;
};
// The IV of the chunk that starts at `sector`.
std::array<std::uint8_t, 16> ctgp_efa_chunk_iv(const CtgpChunkIv& iv, std::uint32_t sector);

// A FAT date and time as seconds since 1970, the way CTGP's code reads them
// (the fields taken as UTC; its stat routine, 0x817a3b9c).
std::uint32_t ctgp_fat_seconds(std::uint16_t time, std::uint16_t date);
// The fixed words of the second package disk's key (jelly.bin, "efb:"):
// words 1 and 3. Word 0 is the file's last-write time and word 2 its
// creation time, as the in-game startup builds them from the file's stat.
// The values are CTGP-R's, read from tables.bin.
struct CtgpEfbKeyWords {
    std::uint32_t w1 = 0, w3 = 0;
};
std::array<std::uint8_t, 16> ctgp_efb_key(const CtgpEfbKeyWords& words, std::uint16_t write_time, std::uint16_t write_date,
                                          std::uint16_t create_time, std::uint16_t create_date);

// A BlockReader (512-byte blocks) over the decrypted image. `size` is the
// encrypted file's length, a multiple of 512; blocks past it fail. The last
// few chunks decrypted are kept. The returned reader owns its state.
BlockReader ctgp_efa_block_reader(ByteReader raw, std::uint64_t size, const std::array<std::uint8_t, 16>& key,
                                  const CtgpChunkIv& iv);

}  // namespace riftwii
