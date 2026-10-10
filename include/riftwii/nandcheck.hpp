// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

// IOS's file system keeps the Wii Menu's files to the Wii Menu: in its
// Thumb code, "cmp r3, r1; beq" before "movs r5, #0x66" (-102). Turning
// the beq into an unconditional branch opens the check for that IOS
// (wii/boot.cpp, open_nand_permissions). The search and the store's
// arithmetic are here so the tests can check them on a copy of memory.
namespace riftwii {

constexpr std::uint8_t kNandCheck[6] = {0x42, 0x8B, 0xD0, 0x01, 0x25, 0x66};

// The first place in `mem` (`size` bytes, 2-byte steps: Thumb code) that
// holds kNandCheck, or false.
bool find_nand_check(const std::uint8_t* mem, std::size_t size, std::size_t& offset);

// The branch (at match + 2) is written with one aligned 32-bit store: a
// 16-bit store through MEM2's uncached mirror may not keep the bytes
// around it (it hung consoles). `word` is that store's address, `shift`
// where the halfword sits in it (big-endian).
struct NandCheckStore {
    std::uint32_t word = 0;
    unsigned shift = 0;
};
NandCheckStore nand_check_store(std::uint32_t match_address);
// The word with the beq (0xD001) made "b +2" (0xE001).
std::uint32_t nand_check_opened(std::uint32_t word, unsigned shift);
// The halfword the store puts there, as read back.
std::uint16_t nand_check_half(std::uint32_t word, unsigned shift);

}  // namespace riftwii
