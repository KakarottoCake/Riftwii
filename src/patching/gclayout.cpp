// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gclayout.hpp"

namespace riftwii {

namespace {

constexpr std::uint32_t kMem1Start = 0x80000000;
constexpr std::uint32_t kMem1End = 0x81800000;
constexpr std::uint32_t kBlr = 0x4E800020;

bool in_mem1(std::uint32_t address) { return address >= kMem1Start && address <= kMem1End; }

// `lis rD,value` (addis rD,0,value): the upper half it loads, or -1.
std::int64_t lis_value(std::uint32_t word, std::uint32_t rd) {
    const std::uint32_t lis = 0x3C000000 | (rd << 21);
    if ((word & 0xFFFF0000) != lis) return -1;
    return static_cast<std::int64_t>(word & 0xFFFF) << 16;
}

// `ori rD,rD,low` or `addi rD,rD,low` after a `lis rD,high`: the whole
// value, or -1 when `word` is neither.
std::int64_t lower_half(std::uint32_t high, std::uint32_t word, std::uint32_t rd) {
    const std::uint32_t ori = 0x60000000 | (rd << 21) | (rd << 16);
    const std::uint32_t addi = 0x38000000 | (rd << 21) | (rd << 16);
    if ((word & 0xFFFF0000) == ori) return high | (word & 0xFFFF);
    if ((word & 0xFFFF0000) == addi) {
        const std::int32_t low = static_cast<std::int16_t>(word & 0xFFFF);
        return static_cast<std::uint32_t>(high + low);
    }
    return -1;
}

// The `lis r1` and `ori r1,r1` (or `addi r1,r1`) pair in the function at
// `start`, up to its first blr. __init_registers zeroes the other
// registers between them in some SDKs, so the lower half may come a few
// instructions after the upper one.
GcStackTop stack_top_in(const GcCodeReader& read, std::uint32_t start, std::uint32_t max_words) {
    GcStackTop found;
    std::int64_t high = -1;
    for (std::uint32_t i = 0; i < max_words; ++i) {
        const std::uint32_t at = start + 4 * i;
        std::uint32_t word = 0;
        if (!read(at, word) || word == kBlr) break;
        const std::int64_t upper = lis_value(word, 1);
        if (upper >= 0) {
            high = upper;
            continue;
        }
        if (high < 0) continue;
        const std::int64_t whole = lower_half(static_cast<std::uint32_t>(high), word, 1);
        if (whole < 0) continue;
        const std::uint32_t top = static_cast<std::uint32_t>(whole);
        if (!in_mem1(top)) return found;
        found.address = top;
        found.set_at = at;
        return found;
    }
    return found;
}

}  // namespace

GcStackTop find_gc_stack_top(const GcCodeReader& read, std::uint32_t entry) {
    // __start itself first (a game may set r1 there), then each function
    // its first instructions call: __init_registers is the first.
    constexpr std::uint32_t kStartWords = 48;
    constexpr std::uint32_t kCalleeWords = 96;
    for (std::uint32_t i = 0; i < kStartWords; ++i) {
        const std::uint32_t at = entry + 4 * i;
        std::uint32_t word = 0;
        if (!read(at, word)) break;
        std::int64_t high = lis_value(word, 1);
        if (high >= 0) {
            const GcStackTop direct = stack_top_in(read, at, 8);
            if (direct.address != 0) return direct;
            continue;
        }
        const bool is_bl = (word & 0xFC000003) == 0x48000001;
        if (!is_bl) continue;
        std::int32_t offset = static_cast<std::int32_t>(word & 0x03FFFFFC);
        if (offset & 0x02000000) offset -= 0x04000000;
        const std::uint32_t callee = at + static_cast<std::uint32_t>(offset);
        const GcStackTop in_callee = stack_top_in(read, callee, kCalleeWords);
        if (in_callee.address != 0) return in_callee;
    }
    return GcStackTop{};
}

std::uint32_t find_gc_arena_lo(const GcCodeReader& read, const std::vector<GcTextRange>& text) {
    for (const GcTextRange& range : text) {
        for (std::uint32_t at = range.start; at + 20 <= range.end; at += 4) {
            std::uint32_t load = 0;
            if (!read(at, load)) continue;
            // lwz rD,0x30(rA): the boot info's arenaLo (rA holds 0x80000000).
            const bool is_lwz_30 = (load >> 26) == 32 && (load & 0xFFFF) == 0x30;
            if (!is_lwz_30) continue;
            const std::uint32_t rd = (load >> 21) & 31;
            std::uint32_t compare = 0, branch = 0, upper = 0, lower = 0;
            if (!read(at + 4, compare) || !read(at + 8, branch) || !read(at + 12, upper) || !read(at + 16, lower))
                continue;
            const bool is_cmplwi_0 = compare == (0x28000000 | (rd << 16));
            const bool is_bc = (branch >> 26) == 16;
            if (!is_cmplwi_0 || !is_bc) continue;
            const std::int64_t high = lis_value(upper, rd);
            if (high < 0) continue;
            const std::int64_t whole = lower_half(static_cast<std::uint32_t>(high), lower, rd);
            if (whole < 0 || !in_mem1(static_cast<std::uint32_t>(whole))) continue;
            return static_cast<std::uint32_t>(whole);
        }
    }
    return 0;
}

GcMem1Plan plan_gc_mem1(std::uint32_t fst_addr, std::vector<GcMem1Piece>& pieces, std::uint32_t bi2_bytes) {
    GcMem1Plan plan;
    plan.di_regs = ((fst_addr - kGcDiRegsBytes - 0x6000) & ~0xFFFFu) + 0x6000;
    const std::uint32_t upper_floor = plan.di_regs + kGcDiRegsBytes;
    std::uint32_t upper_next = fst_addr;      // the room between the stand-in and the FST
    std::uint32_t lower_next = plan.di_regs;  // and from the stand-in down
    for (GcMem1Piece& piece : pieces) {
        const std::uint32_t above = (upper_next - piece.bytes) & ~31u;
        const bool fits_above = upper_next - upper_floor >= piece.bytes && above >= upper_floor;
        if (fits_above) {
            piece.address = above;
            upper_next = above;
        } else {
            piece.address = (lower_next - piece.bytes) & ~31u;
            lower_next = piece.address;
        }
    }
    plan.bi2 = (lower_next - bi2_bytes) & ~31u;
    plan.arena_hi = plan.bi2;
    return plan;
}

}  // namespace riftwii
