// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The virtual SD card (runtime/rtvsd.c): the ioctls, the card commands a
// game's SD driver sends (in the order Super Smash Bros. Brawl sends
// them), the registers and where the image's blocks lie.
#include "rtvsd.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <memory>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << std::hex << (a) << " != " << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

std::unique_ptr<rtvsd_card> make_card(std::uint32_t sectors, std::initializer_list<rtvsd_extent> extents) {
    auto card = std::make_unique<rtvsd_card>();
    std::memset(card.get(), 0, sizeof(rtvsd_card));
    card->sectors = sectors;
    for (const rtvsd_extent& e : extents) card->extents[card->extent_count++] = e;
    return card;
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

}  // namespace

int main() {
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
            rtvsd_csd(card.get(), csd);
            const std::uint32_t stated = rtvsd_csd_sectors(csd);
            EXPECT_TRUE(stated <= sectors);
            EXPECT_TRUE(stated > sectors - sectors / 64);
            EXPECT_EQ(csd[3] & 1u, 1u);                    // end bit
            EXPECT_EQ(csd[0] >> 30, card->sdhc ? 1u : 0u);  // CSD_STRUCTURE
        }
        // A 2 GiB standard card: 2048-byte blocks, C_SIZE 4095, C_SIZE_MULT 6.
        auto two = make_card(0x400000, {{0, 0, 0x400000}});
        rtvsd_reset(two.get());
        std::uint32_t csd[4];
        rtvsd_csd(two.get(), csd);
        EXPECT_EQ((csd[1] >> 16) & 15u, 11u);                             // READ_BL_LEN
        EXPECT_EQ(((csd[1] & 0x3FFu) << 2) | (csd[2] >> 30), 4095u);     // C_SIZE
        EXPECT_EQ((csd[2] >> 15) & 7u, 6u);                               // C_SIZE_MULT
        // 1.5 GiB, as Dolphin states it (Brawl reads nothing past 1 GiB
        // of a card with 1024-byte blocks): C_SIZE 3071, C_SIZE_MULT 6.
        auto one_half = make_card(0x300000, {{0, 0, 0x300000}});
        rtvsd_reset(one_half.get());
        rtvsd_csd(one_half.get(), csd);
        EXPECT_EQ((csd[1] >> 16) & 15u, 11u);
        EXPECT_EQ(((csd[1] & 0x3FFu) << 2) | (csd[2] >> 30), 3071u);
        EXPECT_EQ((csd[2] >> 15) & 7u, 6u);
        // 1 GiB and below: 512-byte blocks.
        auto one = make_card(0x200000, {{0, 0, 0x200000}});
        rtvsd_reset(one.get());
        rtvsd_csd(one.get(), csd);
        EXPECT_EQ((csd[1] >> 16) & 15u, 9u);
        EXPECT_EQ(((csd[1] & 0x3FFu) << 2) | (csd[2] >> 30), 4095u);
        EXPECT_EQ((csd[2] >> 15) & 7u, 7u);
        // An 8 GiB SDHC card: C_SIZE 16383 (512 KiB units).
        auto eight = make_card(0x1000000, {{0, 0, 0x1000000}});
        rtvsd_reset(eight.get());
        rtvsd_csd(eight.get(), csd);
        EXPECT_EQ(((csd[1] & 0x3Fu) << 16) | (csd[2] >> 16), 16383u);
    }
    // CRC7 of a known register: the CID example of the SD specification's
    // CRC7 section works on bytes; check the CRC over our CID recomputed
    // bit by bit here.
    {
        std::uint32_t cid[4];
        rtvsd_cid(cid);
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
        EXPECT_EQ(rtvsd_reset(card.get()), 0);
        rtvsd_answer a = send(card.get(), RTVSD_CMD_EVENT_REGISTER, 2);
        EXPECT_EQ(a.kind, RTVSD_EVENT_HOLD);
        a = send(card.get(), RTVSD_CMD_SEND_CID, RTVSD_RCA << 16);
        EXPECT_EQ(a.kind, RTVSD_DONE);
        EXPECT_EQ(a.reply[0] >> 24, 0x52u);
        a = send(card.get(), RTVSD_CMD_SEND_CSD, RTVSD_RCA << 16);
        EXPECT_EQ(rtvsd_csd_sectors(a.reply), 0x800000u);
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
