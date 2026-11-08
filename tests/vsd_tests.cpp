// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The virtual SD card (runtime/rtvsd.c): the ioctls, the card commands a
// game's SD driver sends (in the order Super Smash Bros. Brawl sends
// them), the registers and where the image's blocks lie. The CID and CSD
// come from the loader (vsd_card_registers); they are checked against the
// builder the blob used to carry (tests/vsd_legacy.c), word for word.
#include "rtvsd.h"
#include "riftwii/vsdbuild.hpp"
extern "C" {
#include "vsd_hook.h"
}

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <vector>

extern "C" {
void legacy_rtvsd_cid(std::uint32_t out[4]);
void legacy_rtvsd_csd(const rtvsd_card* card, std::uint32_t out[4]);
}

using riftwii::vsd_card_registers;
using riftwii::vsd_csd_sectors;

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << std::hex << (a) << " != " << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

// The card with its extent table behind it, as the blob's state lays it out.
struct CardDeleter {
    void operator()(rtvsd_card* card) const { std::free(card); }
};
using CardPtr = std::unique_ptr<rtvsd_card, CardDeleter>;

CardPtr make_card_sized(std::uint32_t sectors, std::size_t capacity) {
    const std::size_t bytes = sizeof(rtvsd_card) + rtvsd_extent_bytes(capacity);
    rtvsd_card* card = static_cast<rtvsd_card*>(std::calloc(1, bytes));
    card->sectors = sectors;
    return CardPtr(card);
}

CardPtr make_card(std::uint32_t sectors, std::initializer_list<rtvsd_extent> extents) {
    CardPtr card = make_card_sized(sectors, extents.size());
    for (const rtvsd_extent& e : extents) rtvsd_extents(card.get())[card->extent_count++] = e;
    return card;
}

// The CSD the loader puts in the card for its size (after rtvsd_reset).
void card_csd(const rtvsd_card* card, std::uint32_t csd[4]) {
    std::uint32_t cid[4];
    vsd_card_registers(card->sectors, card->sdhc != 0, cid, csd);
}

// rtvsd_reset, then the registers, as the loader does (wii/hooks/vsdhook.cpp).
int reset_as_loader(rtvsd_card* card) {
    const int r = rtvsd_reset(card);
    vsd_card_registers(card->sectors, card->sdhc != 0, card->cid, card->csd);
    return r;
}

// The loader's CID and CSD against the blob's old builder, for a size:
// the same four words each.
void expect_registers_as_before(std::uint32_t sectors) {
    auto card = make_card(sectors, {{0, 0, sectors}});
    rtvsd_reset(card.get());
    std::uint32_t cid[4], csd[4], old_cid[4], old_csd[4];
    vsd_card_registers(card->sectors, card->sdhc != 0, cid, csd);
    legacy_rtvsd_cid(old_cid);
    legacy_rtvsd_csd(card.get(), old_csd);
    for (int i = 0; i < 4; ++i) {
        if (cid[i] != old_cid[i] || csd[i] != old_csd[i]) {
            std::cerr << "FAILED: registers differ from the old builder for " << sectors << " sectors, word " << i
                      << std::endl;
            g_failures++;
            return;
        }
    }
}

rtvsd_answer send(rtvsd_card* card, std::uint32_t cmd, std::uint32_t arg, std::uint32_t blocks = 0,
                  std::uint32_t data_len = 0) {
    rtvsd_request req{};
    req.cmd = cmd;
    req.arg = arg;
    req.blk_cnt = blocks;
    req.blk_size = blocks ? 512 : 0;
    rtvsd_answer a;
    rtvsd_command(card, &req, data_len, &a);
    return a;
}

std::uint32_t hcr(rtvsd_card* card, std::uint32_t ioctl, std::uint32_t reg, std::uint32_t size, std::uint32_t value,
                  std::int32_t* result = nullptr) {
    const std::uint32_t in[6] = {reg, 0, 0, size, value, 0};
    std::uint32_t out = 0xDEADBEEF;
    const std::int32_t r = rtvsd_ioctl(card, ioctl, in, sizeof(in), &out, 4);
    if (result) *result = r;
    return out;
}

// ---- The blob's entries (runtime/vsd/vsd_entry.S) ---------------------------
//
// When the Wii build has made the blob (build-runtime/riftwii_vsd.bin), its
// entries are decoded here: a hook on a handle other than the card's must
// branch straight to its own replay slot, the opens must always reach the
// shared trampoline with their own number, and the trampoline must find
// slot n where the header says it is. The host cannot run PowerPC code, so
// this checks the instructions themselves.

std::uint32_t be32(const std::vector<std::uint8_t>& b, std::uint32_t at) {
    if (b.size() < 4 || at > b.size() - 4) return 0;
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

// The target of a relative branch (b or bc) at `at`, or ~0 when the word is not one.
std::uint32_t branch_target(std::uint32_t word, std::uint32_t at) {
    if ((word & 0xFC000003u) == 0x48000000u) {
        std::int32_t delta = static_cast<std::int32_t>(word & 0x03FFFFFCu);
        if (delta & 0x02000000) delta -= 0x04000000;
        return at + static_cast<std::uint32_t>(delta);
    }
    if ((word & 0xFC000003u) == 0x40000000u) {
        std::int32_t delta = static_cast<std::int32_t>(word & 0xFFFCu);
        if (delta & 0x8000) delta -= 0x10000;
        return at + static_cast<std::uint32_t>(delta);
    }
    return ~0u;
}

// True when `n` bytes at `at` lie inside the blob (no wrap-around).
bool in_blob(const std::vector<std::uint8_t>& b, std::uint32_t at, std::uint32_t n) {
    return b.size() >= n && at <= b.size() - n;
}

std::int32_t simm(std::uint32_t word) { return static_cast<std::int16_t>(word & 0xFFFFu); }

void check_blob_entries(const char* path) {
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr) {
        std::cout << "vsd: no blob at " << path << " (the Wii build makes it); entry check skipped" << std::endl;
        return;
    }
    std::vector<std::uint8_t> blob;
    std::uint8_t chunk[4096];
    std::size_t got;
    while ((got = std::fread(chunk, 1, sizeof(chunk), f)) > 0) blob.insert(blob.end(), chunk, chunk + got);
    std::fclose(f);
    EXPECT_EQ(be32(blob, 0), VSD_MAGIC);
    if (be32(blob, 4) != VSD_VERSION) {
        std::cerr << "FAILED: " << path << " is blob version " << be32(blob, 4) << ", the sources are version "
                  << VSD_VERSION << ": it may be from an old build (make -f Makefile.wii, or Makefile.runtime)"
                  << std::endl;
        g_failures++;
        return;
    }
    const std::uint32_t context = be32(blob, offsetof(vsd_header, context_offset));
    std::uint32_t hook[VSD_ENTRIES], replay[VSD_ENTRIES];
    for (std::uint32_t e = 0; e < VSD_ENTRIES; ++e) {
        hook[e] = be32(blob, offsetof(vsd_header, hook) + 4 * e);
        replay[e] = be32(blob, offsetof(vsd_header, replay) + 4 * e);
        EXPECT_EQ(replay[e], replay[0] + 32 * e);
    }
    // Every offset the header gives must lie inside the blob before it is
    // used for arithmetic: a bad blob is reported, not read past.
    bool valid = in_blob(blob, context, 4);
    for (std::uint32_t e = 0; e < VSD_ENTRIES; ++e)
        valid = valid && in_blob(blob, hook[e], 12) && in_blob(blob, replay[e], 4);
    if (!valid) {
        std::cerr << "FAILED: " << path << " has a hook, replay or context offset outside the blob (" << blob.size()
                  << " bytes)" << std::endl;
        g_failures++;
        return;
    }
    const std::uint32_t kLiR11 = 0x39600000u;   // li r11, n
    const std::uint32_t kCmpwiR3 = 0x2C030000u; // cmpwi r3, n
    std::uint32_t common = ~0u;
    for (std::uint32_t e = 0; e < VSD_ENTRIES; ++e) {
        std::uint32_t at = hook[e];
        const bool open = e == VSD_OPEN || e == VSD_OPEN_ASYNC;
        if (!open) {
            EXPECT_EQ(be32(blob, at), kCmpwiR3 | VSD_FAKE_FD);
            const std::uint32_t bne = be32(blob, at + 4);
            EXPECT_EQ(bne & 0xFFFF0003u, 0x40820000u);  // bne cr0
            EXPECT_EQ(branch_target(bne, at + 4), replay[e]);
            at += 8;
        }
        EXPECT_EQ(be32(blob, at), kLiR11 | e);
        const std::uint32_t target = branch_target(be32(blob, at + 4), at + 4);
        if (!in_blob(blob, target, 4)) {
            std::cerr << "FAILED: " << path << " entry " << e << " branches outside the blob" << std::endl;
            g_failures++;
            return;
        }
        if (common == ~0u) common = target;
        EXPECT_EQ(target, common);
    }
    // The shared body: the context's address from `bl 1f; 1: mflr r3;
    // addi r3, r3, K`, then slot 0 from `addi r12, r12, M` on it.
    std::uint32_t context_seen = ~0u, slot0_seen = ~0u;
    bool times_32 = false;
    for (std::uint32_t at = common; in_blob(blob, at, 12) && at < common + 0x100; at += 4) {
        const std::uint32_t w = be32(blob, at);
        if (w == 0x48000005u && be32(blob, at + 4) == 0x7C6802A6u && (be32(blob, at + 8) & 0xFFFF0000u) == 0x38630000u)
            context_seen = at + 4 + static_cast<std::uint32_t>(simm(be32(blob, at + 8)));
        if ((w & 0xFFFF0000u) == 0x398C0000u && context_seen != ~0u)
            slot0_seen = context_seen + static_cast<std::uint32_t>(simm(w));
        if (w == 0x556B2834u) times_32 = true;  // slwi r11, r11, 5
        if (w == 0x4E800020u) break;  // blr: the body's end
    }
    EXPECT_EQ(context_seen, context);
    EXPECT_EQ(slot0_seen, replay[0]);
    EXPECT_TRUE(times_32);
    std::cout << "vsd: the blob's entries checked (" << blob.size() << " bytes)" << std::endl;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1) check_blob_entries(argv[1]);
    // The loader's registers are the blob's old ones, word for word: every
    // size around the edges (the 8 MiB the loader accepts, each CSD 1.0
    // multiplier step, 1 GiB where the blocks grow, 2 GiB where SDHC
    // starts, the largest image) and a spread of others.
    {
        std::uint32_t checked = 0;
        for (std::uint32_t edge : {0x4000u, 0x8000u, 0x10000u, 0x20000u, 0x40000u, 0x80000u, 0x100000u, 0x200000u,
                                   0x300000u, 0x400000u, 0x400400u, 0x800000u, 0x1000000u, 0x3B72400u, 0x80000000u,
                                   0xFFFFFC00u, 0xFFFFFFFFu}) {
            for (std::uint32_t delta : {0u, 1u, 2u, 511u, 512u, 1023u, 1024u, 4095u, 4096u}) {
                if (edge >= delta) {
                    expect_registers_as_before(edge - delta);
                    ++checked;
                }
                if (edge <= 0xFFFFFFFFu - delta) {
                    expect_registers_as_before(edge + delta);
                    ++checked;
                }
            }
        }
        std::uint32_t seed = 0x52575644u;
        for (int i = 0; i < 20000; ++i) {
            seed = seed * 1664525u + 1013904223u;  // a fixed sequence of sizes, the same on every run
            expect_registers_as_before(0x4000u + (seed >> (i % 8)));
            ++checked;
        }
        EXPECT_TRUE(checked > 20000u);
    }
    // The extents must cover the image from sector 0 without gaps.
    {
        auto ok = make_card(3000, {{0, 5000, 1000}, {1000, 9000, 2000}});
        EXPECT_EQ(rtvsd_reset(ok.get()), 0);
        auto gap = make_card(3000, {{0, 5000, 1000}, {1001, 9000, 1999}});
        EXPECT_EQ(rtvsd_reset(gap.get()), -1);
        auto short_of = make_card(3000, {{0, 5000, 1000}});
        EXPECT_EQ(rtvsd_reset(short_of.get()), -1);
        auto none = make_card(3000, {});
        EXPECT_EQ(rtvsd_reset(none.get()), -1);
    }
    // A very scattered image (11796 pieces were seen on a used card, the
    // table now takes up to RTVSD_MAX_EXTENTS): reset accepts it, a lookup
    // finds the right piece at both ends and in the middle, and one more
    // than the limit is refused.
    {
        const std::uint32_t pieces = RTVSD_MAX_EXTENTS;
        const std::uint32_t piece_sectors = 8;
        auto big = make_card_sized(pieces * piece_sectors, pieces);
        for (std::uint32_t i = 0; i < pieces; ++i) {
            // Device pieces in reverse order, so the table is not simply increasing.
            rtvsd_extents(big.get())[i] = {i * piece_sectors, (pieces - i) * 1000u, piece_sectors};
        }
        big->extent_count = pieces;
        EXPECT_EQ(rtvsd_reset(big.get()), 0);
        for (std::uint32_t i : {0u, 1u, 5000u, 11796u, pieces - 2, pieces - 1}) {
            std::uint32_t dev = 0;
            EXPECT_EQ(rtvsd_map(big.get(), i * piece_sectors + 3, 64, &dev), 5u);
            EXPECT_EQ(dev, (pieces - i) * 1000u + 3u);
        }
        big->extent_count = pieces + 1;
        EXPECT_EQ(rtvsd_reset(big.get()), -1);
    }
    // Blocks to device sectors, a run at a time.
    {
        auto card = make_card(3000, {{0, 5000, 1000}, {1000, 9000, 1500}, {2500, 100, 500}});
        EXPECT_EQ(rtvsd_reset(card.get()), 0);
        std::uint32_t dev = 0;
        EXPECT_EQ(rtvsd_map(card.get(), 0, 10, &dev), 10u);
        EXPECT_EQ(dev, 5000u);
        EXPECT_EQ(rtvsd_map(card.get(), 990, 64, &dev), 10u);
        EXPECT_EQ(dev, 5990u);
        EXPECT_EQ(rtvsd_map(card.get(), 1000, 64, &dev), 64u);
        EXPECT_EQ(dev, 9000u);
        EXPECT_EQ(rtvsd_map(card.get(), 2499, 64, &dev), 1u);
        EXPECT_EQ(dev, 10499u);
        EXPECT_EQ(rtvsd_map(card.get(), 2999, 64, &dev), 1u);
        EXPECT_EQ(dev, 599u);
        EXPECT_EQ(rtvsd_map(card.get(), 3000, 1, &dev), 0u);
    }
    // Up to 2 GiB a standard-capacity card (byte addresses), above SDHC.
    {
        auto two = make_card(0x400000, {{0, 0, 0x400000}});
        EXPECT_EQ(rtvsd_reset(two.get()), 0);
        EXPECT_EQ(two->sdhc, 0u);
        auto more = make_card(0x400400, {{0, 0, 0x400400}});
        EXPECT_EQ(rtvsd_reset(more.get()), 0);
        EXPECT_EQ(more->sdhc, 1u);
    }
    // The CSD states the image's size (rounded down to what it can say).
    {
        const std::uint32_t sizes[] = {0x400000, 0x200000, 0x20000, 0x1E8480, 0x800000, 0x1000000, 0x3B72400};
        for (std::uint32_t sectors : sizes) {
            auto card = make_card(sectors, {{0, 0, sectors}});
            EXPECT_EQ(rtvsd_reset(card.get()), 0);
            std::uint32_t csd[4];
            card_csd(card.get(), csd);
            const std::uint32_t stated = vsd_csd_sectors(csd);
            EXPECT_TRUE(stated <= sectors);
            EXPECT_TRUE(stated > sectors - sectors / 64);
            EXPECT_EQ(csd[3] & 1u, 1u);                    // end bit
            EXPECT_EQ(csd[0] >> 30, card->sdhc ? 1u : 0u);  // CSD_STRUCTURE
        }
        // A 2 GiB standard card: 2048-byte blocks, C_SIZE 4095, C_SIZE_MULT 6.
        auto two = make_card(0x400000, {{0, 0, 0x400000}});
        rtvsd_reset(two.get());
        std::uint32_t csd[4];
        card_csd(two.get(), csd);
        EXPECT_EQ((csd[1] >> 16) & 15u, 11u);                             // READ_BL_LEN
        EXPECT_EQ(((csd[1] & 0x3FFu) << 2) | (csd[2] >> 30), 4095u);     // C_SIZE
        EXPECT_EQ((csd[2] >> 15) & 7u, 6u);                               // C_SIZE_MULT
        // 1.5 GiB, as Dolphin states it (Brawl reads nothing past 1 GiB
        // of a card with 1024-byte blocks): C_SIZE 3071, C_SIZE_MULT 6.
        auto one_half = make_card(0x300000, {{0, 0, 0x300000}});
        rtvsd_reset(one_half.get());
        card_csd(one_half.get(), csd);
        EXPECT_EQ((csd[1] >> 16) & 15u, 11u);
        EXPECT_EQ(((csd[1] & 0x3FFu) << 2) | (csd[2] >> 30), 3071u);
        EXPECT_EQ((csd[2] >> 15) & 7u, 6u);
        // 1 GiB and below: 512-byte blocks.
        auto one = make_card(0x200000, {{0, 0, 0x200000}});
        rtvsd_reset(one.get());
        card_csd(one.get(), csd);
        EXPECT_EQ((csd[1] >> 16) & 15u, 9u);
        EXPECT_EQ(((csd[1] & 0x3FFu) << 2) | (csd[2] >> 30), 4095u);
        EXPECT_EQ((csd[2] >> 15) & 7u, 7u);
        // An 8 GiB SDHC card: C_SIZE 16383 (512 KiB units).
        auto eight = make_card(0x1000000, {{0, 0, 0x1000000}});
        rtvsd_reset(eight.get());
        card_csd(eight.get(), csd);
        EXPECT_EQ(((csd[1] & 0x3Fu) << 16) | (csd[2] >> 16), 16383u);
    }
    // CRC7 of a known register: the CID example of the SD specification's
    // CRC7 section works on bytes; check the CRC over our CID recomputed
    // bit by bit here.
    {
        std::uint32_t cid[4], csd[4];
        vsd_card_registers(0x800000, true, cid, csd);
        std::uint8_t bytes[16];
        for (int i = 0; i < 16; ++i) bytes[i] = static_cast<std::uint8_t>(cid[i / 4] >> (24 - 8 * (i % 4)));
        std::uint8_t crc = 0;
        for (int i = 0; i < 15; ++i) {
            for (int b = 7; b >= 0; --b) {
                const int in = (bytes[i] >> b) & 1, top = (crc >> 6) & 1;
                crc = static_cast<std::uint8_t>((crc << 1) & 0x7F);
                if (in ^ top) crc ^= 0x09;
            }
        }
        EXPECT_EQ(bytes[15], static_cast<std::uint8_t>((crc << 1) | 1));
        EXPECT_EQ(bytes[0], 0x52u);
        EXPECT_TRUE(std::memcmp(bytes + 3, "RIFTW", 5) == 0);
    }
    // The host controller: registers read back, the clock turns stable,
    // the software reset finishes at once (Brawl's sequence).
    {
        auto card = make_card(0x800000, {{0, 0, 0x800000}});
        rtvsd_reset(card.get());
        std::int32_t r = 1;
        hcr(card.get(), RTVSD_IOCTL_WRITEHCR, 0x2F, 1, 7, &r);
        EXPECT_EQ(r, 0);
        EXPECT_EQ(hcr(card.get(), RTVSD_IOCTL_READHCR, 0x2F, 1, 0), 0u);
        hcr(card.get(), RTVSD_IOCTL_WRITEHCR, 0x34, 4, 0x013F00C3);
        hcr(card.get(), RTVSD_IOCTL_WRITEHCR, 0x29, 1, 0x0E);
        hcr(card.get(), RTVSD_IOCTL_WRITEHCR, 0x29, 1, 0x0F);
        hcr(card.get(), RTVSD_IOCTL_WRITEHCR, 0x2C, 2, 0x0101);
        EXPECT_EQ(hcr(card.get(), RTVSD_IOCTL_READHCR, 0x2C, 2, 0), 0x0103u);
        EXPECT_EQ(hcr(card.get(), RTVSD_IOCTL_READHCR, 0x34, 4, 0), 0x013F00C3u);
        EXPECT_EQ(hcr(card.get(), RTVSD_IOCTL_READHCR, 0x29, 1, 0), 0x0Fu);
        EXPECT_EQ(hcr(card.get(), RTVSD_IOCTL_READHCR, 0x28, 1, 0), 0u);
        hcr(card.get(), RTVSD_IOCTL_WRITEHCR, 0x28, 1, 2);
        EXPECT_EQ(hcr(card.get(), RTVSD_IOCTL_READHCR, 0x28, 1, 0), 2u);
        EXPECT_EQ(hcr(card.get(), RTVSD_IOCTL_READHCR, 0x29, 1, 0), 0x0Fu);
        hcr(card.get(), RTVSD_IOCTL_READHCR, 0x100, 1, 0, &r);
        EXPECT_EQ(r, RTVSD_EINVAL);
    }
    // GETSTATUS, RESETCARD, GETOCR.
    {
        auto card = make_card(0x800000, {{0, 0, 0x800000}});
        rtvsd_reset(card.get());
        std::uint32_t out = 0;
        EXPECT_EQ(rtvsd_ioctl(card.get(), RTVSD_IOCTL_GETSTATUS, nullptr, 0, &out, 4), 0);
        EXPECT_EQ(out, RTVSD_STATUS_INSERTED | RTVSD_STATUS_INITIALIZED | RTVSD_STATUS_SDHC);
        EXPECT_EQ(rtvsd_ioctl(card.get(), RTVSD_IOCTL_RESETCARD, nullptr, 0, &out, 4), 0);
        EXPECT_EQ(out, RTVSD_RCA << 16);
        EXPECT_EQ(rtvsd_ioctl(card.get(), RTVSD_IOCTL_GETOCR, nullptr, 0, &out, 4), 0);
        EXPECT_EQ(out, 0xC0FF8000u);
        EXPECT_EQ(rtvsd_ioctl(card.get(), RTVSD_IOCTL_SETCLK, nullptr, 0, nullptr, 0), 0);
        EXPECT_EQ(rtvsd_ioctl(card.get(), 0x55, nullptr, 0, &out, 4), RTVSD_EINVAL);
        auto small = make_card(0x1000, {{0, 0, 0x1000}});
        rtvsd_reset(small.get());
        rtvsd_ioctl(small.get(), RTVSD_IOCTL_GETSTATUS, nullptr, 0, &out, 4);
        EXPECT_EQ(out, RTVSD_STATUS_INSERTED | RTVSD_STATUS_INITIALIZED);
    }
    // The game's commands after its reset: event, CID, CSD, select, block
    // length, bus width, then reads and writes.
    {
        auto card = make_card(0x800000, {{0, 1000, 0x400000}, {0x400000, 0x500000, 0x400000}});
        EXPECT_EQ(reset_as_loader(card.get()), 0);
        rtvsd_answer a = send(card.get(), RTVSD_CMD_EVENT_REGISTER, 2);
        EXPECT_EQ(a.kind, RTVSD_EVENT_HOLD);
        a = send(card.get(), RTVSD_CMD_SEND_CID, RTVSD_RCA << 16);
        EXPECT_EQ(a.kind, RTVSD_DONE);
        EXPECT_EQ(a.reply[0] >> 24, 0x52u);
        for (int i = 0; i < 4; ++i) EXPECT_EQ(a.reply[i], card->cid[i]);
        {
            // CMD2 answers the same CID (on a card of its own: it moves the card to IDENT).
            auto other = make_card(0x800000, {{0, 0, 0x800000}});
            EXPECT_EQ(reset_as_loader(other.get()), 0);
            const rtvsd_answer all = send(other.get(), RTVSD_CMD_ALL_SEND_CID, 0);
            const std::uint32_t kStateIdent = 2;  // SD card state "ident" (private to rtvsd.c)
            EXPECT_EQ(other->state, kStateIdent);
            for (int i = 0; i < 4; ++i) EXPECT_EQ(all.reply[i], card->cid[i]);
        }
        a = send(card.get(), RTVSD_CMD_SEND_CSD, RTVSD_RCA << 16);
        EXPECT_EQ(vsd_csd_sectors(a.reply), 0x800000u);
        for (int i = 0; i < 4; ++i) EXPECT_EQ(a.reply[i], card->csd[i]);
        a = send(card.get(), RTVSD_CMD_SELECT, RTVSD_RCA << 16);
        EXPECT_EQ(a.reply[0], (RTVSD_STATE_STBY << 9) | RTVSD_R1_READY_FOR_DATA);
        EXPECT_EQ(card->state, RTVSD_STATE_TRAN);
        a = send(card.get(), RTVSD_CMD_SET_BLOCKLEN, 512);
        EXPECT_EQ(a.reply[0], 0x900u);
        a = send(card.get(), RTVSD_CMD_APP, RTVSD_RCA << 16);
        EXPECT_EQ(a.reply[0], 0x920u);
        a = send(card.get(), RTVSD_ACMD_SET_BUS_WIDTH, 2);
        EXPECT_EQ(a.reply[0], 0x920u);
        EXPECT_EQ(card->bus_width, 2u);
        // Command 6 without CMD55 is not the bus width.
        a = send(card.get(), 6, 0);
        EXPECT_EQ(card->bus_width, 2u);
        a = send(card.get(), RTVSD_CMD_READ_MULTIPLE, 0x3FFFF0, 64, 64 * 512);
        EXPECT_EQ(a.kind, RTVSD_READ);
        EXPECT_EQ(a.sector, 0x3FFFF0u);
        EXPECT_EQ(a.count, 64u);
        EXPECT_EQ(a.reply[0], 0x900u);
        a = send(card.get(), RTVSD_CMD_READ_SINGLE, 5, 1, 512);
        EXPECT_EQ(a.kind, RTVSD_READ);
        EXPECT_EQ(a.count, 1u);
        a = send(card.get(), RTVSD_CMD_WRITE_MULTIPLE, 0x7FFFFF, 1, 512);
        EXPECT_EQ(a.kind, RTVSD_WRITE);
        EXPECT_EQ(a.sector, 0x7FFFFFu);
        // Past the end, or a buffer too small.
        a = send(card.get(), RTVSD_CMD_READ_MULTIPLE, 0x7FFFFF, 2, 1024);
        EXPECT_EQ(a.kind, RTVSD_DONE);
        EXPECT_TRUE(a.result < 0);
        EXPECT_TRUE(a.reply[0] & RTVSD_R1_OUT_OF_RANGE);
        a = send(card.get(), RTVSD_CMD_READ_MULTIPLE, 0, 4, 1024);
        EXPECT_TRUE(a.result < 0);
        card->read_only = 1;
        a = send(card.get(), RTVSD_CMD_WRITE_SINGLE, 0, 1, 512);
        EXPECT_TRUE(a.result < 0);
        a = send(card.get(), RTVSD_CMD_EVENT_UNREGISTER, 0);
        EXPECT_EQ(a.kind, RTVSD_EVENT_RELEASE);
    }
    // A standard card takes byte addresses.
    {
        auto card = make_card(0x200000, {{0, 0, 0x200000}});
        rtvsd_reset(card.get());
        rtvsd_answer a = send(card.get(), RTVSD_CMD_READ_MULTIPLE, 10 * 512, 3, 3 * 512);
        EXPECT_EQ(a.kind, RTVSD_READ);
        EXPECT_EQ(a.sector, 10u);
        a = send(card.get(), RTVSD_CMD_READ_MULTIPLE, 10 * 512 + 4, 3, 3 * 512);
        EXPECT_TRUE(a.result < 0);
        EXPECT_TRUE(a.reply[0] & RTVSD_R1_ADDRESS_ERROR);
        a = send(card.get(), RTVSD_CMD_APP, 0);
        a = send(card.get(), RTVSD_ACMD_SEND_OP_COND, 0x00FF8000);
        EXPECT_EQ(a.reply[0], RTVSD_OCR);
        a = send(card.get(), RTVSD_CMD_SEND_IF_COND, 0x1AA);
        EXPECT_EQ(a.reply[0], 0x1AAu);
        a = send(card.get(), RTVSD_CMD_APP, 0);
        a = send(card.get(), RTVSD_ACMD_SEND_SCR, 0, 1, 8);
        EXPECT_EQ(a.kind, RTVSD_DATA);
        EXPECT_EQ(a.data_bytes, 8u);
        EXPECT_EQ(a.data[1], 0x25u);
    }

    if (g_failures == 0) {
        std::cout << "ALL VSD TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
