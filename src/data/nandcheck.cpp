// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/nandcheck.hpp"

namespace riftwii {

bool find_nand_check(const std::uint8_t* mem, std::size_t size, std::size_t& offset) {
    if (!mem) return false;
    for (std::size_t at = 0; at + sizeof kNandCheck <= size; at += 2) {
        bool same = true;
        for (std::size_t i = 0; same && i < sizeof kNandCheck; ++i) same = mem[at + i] == kNandCheck[i];
        if (same) {
            offset = at;
            return true;
        }
    }
    return false;
}

NandCheckStore nand_check_store(std::uint32_t match_address) {
    NandCheckStore s;
    s.word = (match_address + 2) & ~3u;
    s.shift = ((match_address + 2) & 3) == 0 ? 16 : 0;
    return s;
}

std::uint32_t nand_check_opened(std::uint32_t word, unsigned shift) {
    return (word & ~(0xFFFFu << shift)) | (0xE001u << shift);
}

std::uint16_t nand_check_half(std::uint32_t word, unsigned shift) {
    return static_cast<std::uint16_t>(word >> shift);
}

}  // namespace riftwii
