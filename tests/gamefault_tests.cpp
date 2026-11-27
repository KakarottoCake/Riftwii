// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A game's crash record (runtime/rtfault.h): which exceptions count, its
// NAND path, and the text the menu makes of it.
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/gamefault.hpp"
#include "riftwii/symsearch.hpp"
#include "rtfault.h"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using riftwii::describe_game_fault;

static bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

static void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

int main() {
    static_assert(sizeof(rtfault_record) == RTFAULT_RECORD_BYTES, "the record is 512 bytes");
    // Crashes, and what is not one.
    EXPECT_TRUE(rtfault_is_crash(RTFAULT_DSI, 0));
    EXPECT_TRUE(rtfault_is_crash(RTFAULT_ISI, 0));
    EXPECT_TRUE(rtfault_is_crash(RTFAULT_PROGRAM, 0x00020000));
    EXPECT_FALSE(rtfault_is_crash(RTFAULT_PROGRAM, RTFAULT_SRR1_FPE));
    EXPECT_FALSE(rtfault_is_crash(RTFAULT_DECREMENTER, 0));
    EXPECT_FALSE(rtfault_is_crash(0, 0));
    EXPECT_TRUE(rtfault_in_ram(0x80001234));
    EXPECT_TRUE(rtfault_in_ram(0x93FFFFFC));
    EXPECT_FALSE(rtfault_in_ram(0x81800000));
    EXPECT_FALSE(rtfault_in_ram(0x80001235));
    EXPECT_FALSE(rtfault_in_ram(0));

    char path[64];
    std::memset(path, 'x', sizeof(path));
    rtfault_path(path);
    EXPECT_EQ(std::string(path), "/shared2/riftwii/crash.bin");

    // The context's words land in their fields (no memory read here).
    {
        std::uint32_t context[0x68] = {};
        for (unsigned i = 0; i < 32; ++i) context[i] = 0x1000 + i;
        context[RTFAULT_CTX_CR] = 0x22;
        context[RTFAULT_CTX_LR] = 0x80001230;
        context[RTFAULT_CTX_SRR0] = 0x80004560;
        context[RTFAULT_CTX_SRR1] = 0x3032;
        rtfault_record r;
        rtfault_fill(&r, RTFAULT_DSI, context, 0x0A000000, 0x1234, "RMCE01\x00\x02", 1, 2, 60750000, 0);
        EXPECT_EQ(r.magic, RTFAULT_MAGIC);
        EXPECT_EQ(r.srr0, 0x80004560u);
        EXPECT_EQ(r.lr, 0x80001230u);
        EXPECT_EQ(r.cr, 0x22u);
        EXPECT_EQ(r.gpr[31], 0x101Fu);
        EXPECT_EQ(r.dar, 0x1234u);
        EXPECT_EQ(r.frame_count, 0u);
        EXPECT_EQ(std::string(r.game, 6), "RMCE01");
        EXPECT_EQ(r.game[7], 2);
    }

    // The text, from a big-endian record as the Wii writes it.
    {
        std::vector<std::uint8_t> b(RTFAULT_RECORD_BYTES, 0);
        put32(b, 0, RTFAULT_MAGIC);
        put32(b, 4, RTFAULT_VERSION);
        std::memcpy(&b[offsetof(rtfault_record, game)], "RMCE01", 6);
        put32(b, offsetof(rtfault_record, exception), RTFAULT_DSI);
        put32(b, offsetof(rtfault_record, srr0), 0x801A2B3C);
        put32(b, offsetof(rtfault_record, dsisr), 0x02000000);  // a store
        put32(b, offsetof(rtfault_record, dar), 0x00000010);
        put32(b, offsetof(rtfault_record, lr), 0x801A2B00);
        put32(b, offsetof(rtfault_record, gpr) + 4, 0x8055AA00);
        // 2026-09-29 12:00:00 in seconds since 2000, at 60.75 MHz.
        const std::uint64_t seconds = 1790683200ull - 946684800ull;
        const std::uint64_t tb = seconds * 60750000ull;
        put32(b, offsetof(rtfault_record, tb_hi), static_cast<std::uint32_t>(tb >> 32));
        put32(b, offsetof(rtfault_record, tb_lo), static_cast<std::uint32_t>(tb));
        put32(b, offsetof(rtfault_record, ticks_per_second), 60750000);
        put32(b, offsetof(rtfault_record, frame_count), 2);
        put32(b, offsetof(rtfault_record, frames), 0x80010000);
        put32(b, offsetof(rtfault_record, frames) + 4, 0x80020000);
        put32(b, offsetof(rtfault_record, code_count), RTFAULT_CODE_WORDS);
        for (unsigned i = 0; i < RTFAULT_CODE_WORDS; ++i) put32(b, offsetof(rtfault_record, code) + i * 4, 0x60000000 + i);
        std::memcpy(&b[offsetof(rtfault_record, riftwii)], "2.4.3-beta", 10);
        std::string text, error;
        EXPECT_TRUE(describe_game_fault(b.data(), b.size(), text, error));
        EXPECT_TRUE(has(text, "The game crashed: RMCE01 (disc 0, version 0), under RiftWii 2.4.3-beta\n"));
        EXPECT_TRUE(has(text, "When: 2026-09-29 12:00:00"));
        EXPECT_TRUE(has(text, "What: bad data address (DSI) (exception 2) at 801A2B3C, writing to 00000010\n"));
        EXPECT_TRUE(has(text, "LR 801A2B00"));
        EXPECT_TRUE(has(text, "R01 8055AA00"));
        EXPECT_TRUE(has(text, "Stack: 80010000 80020000\n"));
        EXPECT_TRUE(has(text, "Code at 801A2B2C: 60000000 60000001 60000002 60000003 [60000004] 60000005"));
        // Not a record.
        b[0] = 0;
        EXPECT_FALSE(describe_game_fault(b.data(), b.size(), text, error));
        EXPECT_FALSE(describe_game_fault(b.data(), 100, text, error));
    }

    // Finding __OSUnhandledException by its string: through a string
    // pool's base (lis/addi, then addi rX, base, offset), or directly.
    {
        std::vector<std::uint8_t> data(0x100, 0);
        const char kText[] = "Unhandled Exception %d";
        std::memcpy(&data[0x24], kText, sizeof(kText));
        const std::vector<riftwii::CodeRange> strings = {{0x80301000, data.data(), data.size()}};
        const auto words = [](std::initializer_list<std::uint32_t> list) {
            std::vector<std::uint8_t> out;
            for (std::uint32_t w : list) {
                for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>(w >> s));
            }
            return out;
        };
        const std::uint32_t kPrologue = 0x9421FFE0, kMflr = 0x7C0802A6, kNop = 0x60000000, kBlr = 0x4E800020;
        // Another function first, then the one: pool base in r31, the
        // string at base + 0x24.
        std::vector<std::uint8_t> pooled = words({kPrologue, kMflr, kNop, kBlr, kPrologue, kMflr, 0x3FE08030,
                                                  0x3BFF1000, kNop, 0x387F0024, kNop, kBlr});
        std::vector<riftwii::CodeRange> text = {{0x80004000, pooled.data(), pooled.size()}};
        std::uint32_t found = 0;
        std::string error;
        EXPECT_TRUE(riftwii::find_unhandled_exception(text, strings, found, error));
        EXPECT_EQ(found, 0x80004010u);
        // Directly: lis r3 / addi r3.
        std::vector<std::uint8_t> direct = words({kPrologue, kMflr, 0x3C608030, 0x38631024, kBlr});
        text = {{0x80005000, direct.data(), direct.size()}};
        EXPECT_TRUE(riftwii::find_unhandled_exception(text, strings, found, error));
        EXPECT_EQ(found, 0x80005000u);
        // Two functions use it: refused.
        std::vector<std::uint8_t> twice = words({kPrologue, kMflr, 0x3C608030, 0x38631024, kBlr, kPrologue, kMflr,
                                                 0x3C608030, 0x38631024, kBlr});
        text = {{0x80006000, twice.data(), twice.size()}};
        EXPECT_FALSE(riftwii::find_unhandled_exception(text, strings, found, error));
        // No string: refused.
        const std::vector<riftwii::CodeRange> none = {{0x80301000, data.data(), 0x20}};
        text = {{0x80005000, direct.data(), direct.size()}};
        EXPECT_FALSE(riftwii::find_unhandled_exception(text, none, found, error));
    }

    if (g_failures == 0) {
        std::cout << "ALL GAME FAULT TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
