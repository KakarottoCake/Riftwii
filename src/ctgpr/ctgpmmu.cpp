// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/ctgpmmu.hpp"

namespace riftwii {
namespace {

constexpr std::uint32_t kPtegMask = 0x7FF;  // 128 KB table: 2048 groups of 8 entries

std::uint32_t load(const std::vector<std::uint8_t>& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

void put(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

// The channel's PTE insert (0x809557a4): the first free slot of the primary
// group, else of the secondary (H set). Referenced always; changed and
// read/write unless `read_only` (PP 3); uncached = I and G.
bool insert(std::vector<std::uint8_t>& htab, std::uint32_t ea, std::uint32_t pa, std::uint32_t vsid, bool read_only,
            bool uncached) {
    const std::uint32_t page = (ea >> 12) & 0xFFFF;
    const std::uint32_t hash = (page ^ vsid) & kPtegMask;
    for (std::uint32_t secondary = 0; secondary < 2; ++secondary) {
        const std::uint32_t group = (secondary ? ~hash : hash) & kPtegMask;
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const std::size_t at = group * 64 + slot * 8;
            if (load(htab, at) & 0x80000000u) continue;
            put(htab, at, 0x80000000u | ((vsid & 0xFFFFFF) << 7) | (secondary << 6) | (page >> 10));
            put(htab, at + 4,
                (pa & 0xFFFFF000u) | 0x100 | (read_only ? 0 : 0x80) | (uncached ? 0x28 : 0) | (read_only ? 3 : 2));
            return true;
        }
    }
    return false;
}

bool fill(const std::vector<std::uint32_t>& low, unsigned r, const std::array<std::uint32_t, 5>& vsid, CtgpMmu& m) {
    const std::uint32_t vsid8 = vsid[0], vsid12 = vsid[2], vsid14 = vsid[4];
    m.htab.assign(kCtgpHtabSize, 0);
    m.entries = 0;
    auto add = [&](std::uint32_t ea, std::uint32_t pa, std::uint32_t v, bool ro, bool uncached) {
        if (!insert(m.htab, ea, pa, v, ro, uncached)) return false;
        ++m.entries;
        return true;
    };
    const std::uint32_t text_start = low[21 + r], text_end = low[24 + r];  // main.dol text
    const std::uint32_t page_copy = low[27 + r], rel_end = low[30 + r];     // StaticR's text from the copied page on
    // The channel's loop (0x80957664..): every MEM1 page.
    for (std::uint32_t ea = 0x80000000u; ea != 0x81800000u; ea += 0x1000) {
        if (ea - 0x81600001u <= 0x1FFFEu) continue;  // the table itself (its first page stays, as in the channel)
        const std::uint32_t identity = ea - 0x80000000u;
        std::uint32_t pa = identity;
        // The low memory the core relocates (the channel does this whenever
        // its bias is set, which is always).
        if (ea <= 0x80001FFFu) pa = kCtgpLowCopy + identity;
        if (ea == page_copy) pa = kCtgpPageCopy;
        const bool boat = ea - 0x81760000u <= 0x8FFFFu;
        const bool low_pages = ea - 0x80001000u <= 0x1FFFu;
        bool protect;  // the channel's r28
        if (ea >= text_start && ea < text_end) protect = true;
        else if (ea >= page_copy && ea < rel_end) protect = true;
        else if (low_pages) protect = true;
        else protect = boat;
        // Read-only: the game's code. The channel also protects the BOAT here
        // and gives CTGP a writable view of it at its bias; with no bias this
        // is the BOAT's only mapping, so it stays writable.
        const bool ro = protect && pa == identity && !low_pages && !boat;
        if (ea - 0x80C00000u > 0x7FFFFFu && !add(ea, pa, vsid8, ro, false)) return false;  // 0x80C00000-0x813FFFFF: BATs
        if (!add(ea + 0x40000000u, pa, vsid12, ro, true)) return false;
    }
    // Hardware registers, uncached.
    for (std::uint32_t ea : {0xCC000000u, 0xCC001000u, 0xCC002000u, 0xCC003000u, 0xCC004000u, 0xCC005000u,
                             0xCC008000u, 0xCD000000u, 0xCD800000u, 0xCD006000u, 0xCD806000u})
        if (!add(ea, ea - 0xC0000000u, vsid12, false, true)) return false;
    // The locked cache, 16 KB at 0xE0000000.
    for (std::uint32_t ea = 0xE0000000u; ea != 0xE0004000u; ea += 0x1000)
        if (!add(ea, ea, vsid14, false, false)) return false;
    m.sdr1 = (kCtgpHtabBase - 0x80000000u) | 1;
    m.sr.fill(0x80000000u);  // T: nothing but the BATs
    const unsigned segments[5] = {8, 9, 12, 13, 14};
    for (int i = 0; i < 5; ++i) m.sr[segments[i]] = 0x10000000u | (vsid[i] & 0xFFFFFF);  // N: no execute
    m.dbat0u = 0x80C0007F;  // 0x80C00000, 4 MB
    m.dbat0l = 0x00C00002;
    m.dbat7u = 0x800003FF;  // 0x80000000, 32 MB: the start-up mapping
    m.dbat7l = 0x00000002;
    m.core_dbat7u = 0x8100007F;  // 0x81000000, 4 MB
    m.core_dbat7l = 0x01000002;
    return true;
}

}  // namespace

bool ctgp_build_mmu(const std::vector<std::uint32_t>& low, CtgpRegion region, CtgpMmu& out, std::string& error) {
    if (low.size() != 54) {
        error = "the row table has no region words (an old rows.bin)";
        return false;
    }
    const unsigned r = static_cast<unsigned>(region);
    // Fixed VSIDs; the channel draws new ones when a group fills up, so does this.
    for (std::uint32_t seed = 0; seed < 64; ++seed) {
        std::array<std::uint32_t, 5> vsid{};
        for (std::uint32_t i = 0; i < 5; ++i) vsid[i] = (0x5A3C00u + seed * 0x1F1u + i * 0x2B7u) & 0xFFFFFF;
        CtgpMmu m;
        if (fill(low, r, vsid, m)) {
            out = std::move(m);
            return true;
        }
    }
    error = "the page table has no room for the memory map";
    return false;
}

bool ctgp_mmu_translate(const CtgpMmu& mmu, std::uint32_t ea, std::uint32_t& pa, bool& read_only) {
    const std::uint32_t sr = mmu.sr[ea >> 28];
    if (sr & 0x80000000u) return false;
    const std::uint32_t vsid = sr & 0xFFFFFF;
    const std::uint32_t page = (ea >> 12) & 0xFFFF;
    const std::uint32_t hash = (page ^ vsid) & kPtegMask;
    for (std::uint32_t secondary = 0; secondary < 2; ++secondary) {
        const std::uint32_t group = (secondary ? ~hash : hash) & kPtegMask;
        const std::uint32_t want = 0x80000000u | (vsid << 7) | (secondary << 6) | (page >> 10);
        for (std::uint32_t slot = 0; slot < 8; ++slot) {
            const std::size_t at = group * 64 + slot * 8;
            if (load(mmu.htab, at) != want) continue;
            const std::uint32_t w1 = load(mmu.htab, at + 4);
            pa = (w1 & 0xFFFFF000u) | (ea & 0xFFF);
            read_only = (w1 & 3) == 3;
            return true;
        }
    }
    return false;
}

}  // namespace riftwii
