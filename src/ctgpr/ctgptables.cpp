// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgptables.hpp"

namespace riftwii {
namespace {

void put_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

// CTGP-R's package-disk crypto constants are four of its own PPC instruction
// words (store-word and blr opcodes), not keys anyone chose: the channel
// assembles them in place and uses them as the AES inputs. blob.bin's key is
// all four; its chunk IV reuses two of them around the chunk's first sector;
// jelly.bin's per-file key reuses the key's 2nd and 4th words around the file's
// timestamps. Kept as the opcode words so it is clear what they are.
constexpr std::uint32_t kBlobKeyWords[4] = {0x90830004u, 0x90a30008u, 0x90c3000cu, 0x4e800020u};
constexpr std::uint32_t kBlobIvWord0 = 0x80630004u;
constexpr std::uint32_t kBlobIvWord1 = 0x90830004u;
constexpr std::uint32_t kBlobIvWord3 = 0x4e800020u;

}  // namespace

std::array<std::uint8_t, 16> ctgp_blob_key() {
    std::array<std::uint8_t, 16> key{};
    for (int w = 0; w < 4; ++w) put_be32(key.data() + 4 * w, kBlobKeyWords[w]);
    return key;
}

CtgpChunkIv ctgp_blob_chunk_iv() {
    CtgpChunkIv iv;
    iv.w0 = kBlobIvWord0;
    iv.w1 = kBlobIvWord1;
    iv.w3 = kBlobIvWord3;
    return iv;
}

CtgpEfbKeyWords ctgp_jelly_key_words() {
    CtgpEfbKeyWords words;
    words.w1 = kBlobKeyWords[1];  // blob key word 1
    words.w3 = kBlobKeyWords[3];  // blob key word 3 (blr)
    return words;
}

}  // namespace riftwii
