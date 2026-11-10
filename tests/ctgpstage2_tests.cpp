// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The channel-program decoder (riftwii/ctgpstage2.hpp). Everything here is
// small and made up: hand-assembled programs that use the same kinds of
// instruction, a table built by a made-up rule, and a made-up "channel
// program" packed with zlib at test time.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <zlib.h>

#include "riftwii/ctgpstage2.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using u32 = std::uint32_t;

// --- a few PowerPC encodings -------------------------------------------------
static u32 d_form(u32 op, u32 d, u32 a, u32 imm) { return (op << 26) | (d << 21) | (a << 16) | (imm & 0xFFFF); }
static u32 x_form(u32 xo, u32 s, u32 a, u32 b) { return (31u << 26) | (s << 21) | (a << 16) | (b << 11) | (xo << 1); }
static u32 li(u32 d, u32 imm) { return d_form(14, d, 0, imm); }
static u32 addi(u32 d, u32 a, u32 imm) { return d_form(14, d, a, imm); }
static u32 lis(u32 d, u32 imm) { return d_form(15, d, 0, imm); }
static u32 ori(u32 a, u32 s, u32 imm) { return d_form(24, s, a, imm); }
static u32 andi_(u32 a, u32 s, u32 imm) { return d_form(28, s, a, imm); }
static u32 mulli(u32 d, u32 a, u32 imm) { return d_form(7, d, a, imm); }
static u32 lbz(u32 d, u32 a, u32 imm) { return d_form(34, d, a, imm); }
static u32 lwz(u32 d, u32 a, u32 imm) { return d_form(32, d, a, imm); }
static u32 stb(u32 s, u32 a, u32 imm) { return d_form(38, s, a, imm); }
static u32 stw(u32 s, u32 a, u32 imm) { return d_form(36, s, a, imm); }
static u32 stwu(u32 s, u32 a, u32 imm) { return d_form(37, s, a, imm); }
static u32 mr(u32 a, u32 s) { return x_form(444, s, a, s); }
static u32 lbzx(u32 d, u32 a, u32 b) { return x_form(87, d, a, b); }
static u32 lwzx(u32 d, u32 a, u32 b) { return x_form(23, d, a, b); }
static u32 stbx(u32 s, u32 a, u32 b) { return x_form(215, s, a, b); }
static u32 dcbf(u32 b) { return x_form(86, 0, 0, b); }
static u32 icbi(u32 b) { return x_form(982, 0, 0, b); }
static u32 mtspr(u32 spr, u32 s) { return (31u << 26) | (s << 21) | ((spr & 31) << 16) | ((spr >> 5) << 11) | (467u << 1); }
static u32 mfspr(u32 d, u32 spr) { return (31u << 26) | (d << 21) | ((spr & 31) << 16) | ((spr >> 5) << 11) | (339u << 1); }
static u32 bl(u32 from, u32 to) { return (18u << 26) | ((to - from) & 0x03FFFFFC) | 1; }
static u32 b(u32 from, u32 to) { return (18u << 26) | ((to - from) & 0x03FFFFFC); }
static u32 bdnz(u32 from, u32 to) { return (16u << 26) | (16u << 21) | ((to - from) & 0xFFFC); }
static constexpr u32 kSync = 0x7c0004ac, kIsync = 0x4c00012c, kBlr = 0x4e800020, kNop = 0x60000000;
static constexpr u32 kIabr = 1010, kDabr = 1013, kCtr = 9;

// --- a made-up DOL -------------------------------------------------------------
struct Section {
    u32 address;
    std::vector<std::uint8_t> bytes;
    bool text;
};
static std::vector<std::uint8_t> make_dol(const std::vector<Section>& sections, u32 bss = 0, u32 bss_size = 0) {
    std::vector<std::uint8_t> dol(0x100, 0);
    auto put = [&](std::size_t at, u32 v) {
        for (int i = 0; i < 4; ++i) dol[at + i] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
    };
    int text = 0, data = 7;
    for (const Section& s : sections) {
        const int slot = s.text ? text++ : data++;
        put(4 * slot, static_cast<u32>(dol.size()));
        put(0x48 + 4 * slot, s.address);
        put(0x90 + 4 * slot, static_cast<u32>(s.bytes.size()));
        dol.insert(dol.end(), s.bytes.begin(), s.bytes.end());
    }
    put(0xd8, bss);
    put(0xdc, bss_size);
    return dol;
}
static std::vector<std::uint8_t> words(const std::vector<u32>& w) {
    std::vector<std::uint8_t> out;
    for (u32 v : w)
        for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (24 - 8 * i)));
    return out;
}

// --- the interpreter's model ---------------------------------------------------
static void test_cache_and_breakpoint_registers() {
    const u32 base = 0x80040000, helper = base + 0x100;
    std::vector<u32> code(0x48, kNop);
    auto at = [&](u32 address, u32 w) { code[(address - base) / 4] = w; };
    u32 pc = base;
    auto emit = [&](u32 w) { at(pc, w); pc += 4; };
    emit(bl(pc, helper));                 // runs the helper once: its line is cached
    emit(mr(20, 3));
    emit(lis(9, helper >> 16));
    emit(ori(9, 9, helper & 0xFFFF));
    emit(lis(10, 0x3860));                // li r3,2 over the helper's first word
    emit(ori(10, 10, 2));
    emit(stw(10, 9, 0));
    emit(dcbf(9));                        // written back, but the cached line stays
    emit(kSync);
    emit(kIsync);
    emit(bl(pc, helper));                 // still the old helper
    emit(mr(21, 3));
    emit(icbi(9));                        // now the line is fetched again
    emit(kIsync);
    emit(bl(pc, helper));
    emit(mr(22, 3));
    emit(lis(10, 0x3860));                // li r3,3, stored but never written back
    emit(ori(10, 10, 3));
    emit(stw(10, 9, 0));
    emit(icbi(9));
    emit(bl(pc, helper));                 // the fetch sees memory, which still has li r3,2
    emit(mr(25, 3));
    emit(li(3, 0x1235));
    emit(mtspr(kIabr, 3));
    emit(mfspr(23, kIabr));               // bit 31 reads back as 0
    emit(li(3, 0xbd));
    emit(mtspr(kDabr, 3));
    emit(mfspr(24, kDabr));               // all of DABR reads back
    emit(lwz(26, 9, 0));                  // a load sees the data view: li r3,3
    const u32 stop = pc;
    at(helper, li(3, 1));
    at(helper + 4, kBlr);
    const auto dol = make_dol({{base, words(code), true}});
    CtgpDolLayout layout;
    std::string error;
    EXPECT_TRUE(ctgp_parse_dol(dol.data(), dol.size(), layout, error));
    CtgpPpc cpu(dol.data(), layout);
    cpu.r[1] = kCtgpPpcStackTop - 0x100;
    cpu.pc = base;
    EXPECT_TRUE(cpu.run_until(stop, 1000, error));
    EXPECT_EQ(cpu.r[20], 1u);
    EXPECT_EQ(cpu.r[21], 1u);           // stale: the cached line
    EXPECT_EQ(cpu.r[22], 2u);           // after icbi
    EXPECT_EQ(cpu.r[25], 2u);           // a store not written back is not fetched
    EXPECT_EQ(cpu.r[23], 0x1234u);
    EXPECT_EQ(cpu.r[24], 0xbdu);
    EXPECT_EQ(cpu.r[26], 0x38600003u);
}

static void test_refusals() {
    const u32 base = 0x80040000;
    std::string error;
    CtgpDolLayout layout;
    auto run = [&](const std::vector<u32>& code, u32 stop, u32 limit) {
        const auto dol = make_dol({{base, words(code), true}});
        EXPECT_TRUE(ctgp_parse_dol(dol.data(), dol.size(), layout, error));
        CtgpPpc cpu(dol.data(), layout);
        cpu.r[1] = kCtgpPpcStackTop - 0x100;
        cpu.pc = base;
        error.clear();
        const bool ok = cpu.run_until(stop, limit, error);
        return ok || error.empty() ? std::string() : error;
    };
    // an instruction it does not know (a floating-point add)
    EXPECT_TRUE(run({kNop, 0xFC21102A, kNop}, base + 8, 100).find("does not run") != std::string::npos);
    // the step limit
    EXPECT_TRUE(run({kNop, b(base + 4, base + 4)}, base + 8, 500).find("step limit") != std::string::npos);
    // a load outside its memory, and a store
    EXPECT_TRUE(run({lis(3, 0x9000), lwz(4, 3, 0), kNop}, base + 8, 100).find("outside") != std::string::npos);
    EXPECT_TRUE(run({lis(3, 0x8170), stw(4, 3, 0), kNop}, base + 8, 100).find("outside") != std::string::npos);
    // running off the end of the code
    EXPECT_TRUE(run({kNop}, base + 0x40, 100).find("outside") != std::string::npos);
    // the store overlay holds 1024 lines: 1000 distinct lines are fine, 1100 are refused
    auto fill_lines = [&](u32 count) {
        return run({lis(4, 0x80ff), li(5, count), mtspr(kCtr, 5), stw(5, 4, 0), addi(4, 4, 32),
                    bdnz(base + 20, base + 12), kNop},
                   base + 24, 10000);
    };
    EXPECT_TRUE(fill_lines(1000).empty());
    EXPECT_TRUE(fill_lines(1100).find("full") != std::string::npos);
    // its stack works
    EXPECT_TRUE(run({stwu(1, 1, 0xFFE0), stw(1, 1, 8), lwz(5, 1, 8), kNop}, base + 12, 100).empty());
    // a malformed DOL
    std::vector<std::uint8_t> bad = make_dol({{base, words({kNop}), true}});
    bad[3] = 0xF0;  // the text section's offset past the end
    EXPECT_TRUE(!ctgp_parse_dol(bad.data(), bad.size(), layout, error));
    EXPECT_TRUE(!ctgp_parse_dol(bad.data(), 16, layout, error));
}

// --- a made-up packed channel program -----------------------------------------
constexpr u32 kTableAt = 0x80060000, kPayloadAt = 0x80070000;
constexpr u32 kCupsAt = kCtgpChannelBase + 0x200;

// The plain channel program: the settings step's instructions, then the
// three layouts at kCupsAt.
static std::vector<std::uint8_t> channel_program(bool with_step) {
    std::vector<std::uint8_t> out(0x200, 0);
    if (with_step) {
        const auto step = words({lis(8, kCupsAt >> 16), lwz(7, 13, 0xb4), mulli(9, 7, 0xdc), addi(8, 8, kCupsAt & 0xFFFF),
                                 kNop, lwzx(3, 8, 9), kNop, stw(3, 31, 0xfed8), kBlr});
        std::memcpy(out.data() + 0x40, step.data(), step.size());
    }
    std::vector<std::uint8_t> cups(3 * kCtgpCupEntryBytes, 0xff);
    const u32 counts[3] = {2, 1, 0};
    for (int e = 0; e < 3; ++e) {
        std::uint8_t* entry = cups.data() + e * kCtgpCupEntryBytes;
        std::memset(entry, 0, 4);
        entry[3] = static_cast<std::uint8_t>(counts[e]);
        for (u32 i = 0; i < counts[e] * 4; ++i) entry[4 + i] = static_cast<std::uint8_t>(0x20 + 10 * e + i);
    }
    out.insert(out.end(), cups.begin(), cups.end());
    out.resize(out.size() + 0x100, 0x5a);  // more program after the table
    return out;
}

static std::vector<std::uint8_t> raw_deflate(const std::vector<std::uint8_t>& plain) {
    z_stream z{};
    deflateInit2(&z, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
    std::vector<std::uint8_t> out(plain.size() + 1024);
    z.next_in = const_cast<std::uint8_t*>(plain.data());
    z.avail_in = static_cast<uInt>(plain.size());
    z.next_out = out.data();
    z.avail_out = static_cast<uInt>(out.size());
    deflate(&z, Z_FINISH);
    out.resize(z.total_out);
    deflateEnd(&z);
    return out;
}

// The made-up rule the made-up builder uses: table[i] = 7 + step * i.
static std::array<std::uint8_t, 256> rule_table(u32 step) {
    std::array<std::uint8_t, 256> t{};
    for (u32 i = 0; i < 256; ++i) t[i] = static_cast<std::uint8_t>(7 + step * i);
    return t;
}

// MAIN.DOL in miniature: a function that builds the table by the rule, then
// the substitution loop over the payload; the payload is the program packed
// with the rule's inverse, so the loop unpacks it.
static std::vector<std::uint8_t> stage2(u32 step, bool with_loop, bool with_step, bool damage = false) {
    const u32 base = 0x80040000;
    std::vector<u32> code;
    auto here = [&]() { return base + 4 * static_cast<u32>(code.size()); };
    code.push_back(stwu(1, 1, 0xFFE0));
    code.push_back(lis(31, kTableAt >> 16));
    code.push_back(li(4, 0));
    code.push_back(li(5, 7));
    code.push_back(li(6, 256));
    code.push_back(mtspr(kCtr, 6));
    const u32 fill = here();
    code.push_back(stbx(5, 31, 4));
    code.push_back(addi(4, 4, 1));
    code.push_back(addi(5, 5, step));
    code.push_back(andi_(5, 5, 255));
    code.push_back(bdnz(here(), fill));
    // the payload
    const auto deflated = raw_deflate(channel_program(with_step));
    std::vector<std::uint8_t> payload(10, 0x11);
    payload.insert(payload.end(), deflated.begin(), deflated.end());
    payload.resize(payload.size() + 16, 0x22);  // what follows the stream
    code.push_back(lis(9, kPayloadAt >> 16));
    code.push_back(lis(10, 0));
    code.push_back(ori(10, 10, static_cast<u32>(payload.size())));
    code.push_back(mtspr(kCtr, 10));
    if (with_loop) {
        const u32 loop = here();
        code.push_back(andi_(10, 9, 15));
        code.push_back(0x41820010);
        code.push_back(lbz(10, 9, 0));
        code.push_back(lbzx(10, 31, 10));
        code.push_back(stb(10, 9, 0));
        code.push_back(addi(9, 9, 1));
        code.push_back(bdnz(here(), loop));
    }
    code.push_back(kBlr);
    // pack: the loop maps byte b to table[b], so store the inverse
    const auto table = rule_table(step);
    std::array<std::uint8_t, 256> inverse{};
    for (u32 i = 0; i < 256; ++i) inverse[table[i]] = static_cast<std::uint8_t>(i);
    for (u32 i = 0; i < payload.size(); ++i)
        if (((kPayloadAt + i) & 15) != 0) payload[i] = inverse[payload[i]];
    if (damage) payload[40] ^= 0x5a;
    return make_dol({{base, words(code), true},
                     {kTableAt, std::vector<std::uint8_t>(256, 0), false},
                     {kPayloadAt, payload, false}},
                    0x80080000, 0x1000);
}

static void test_decode() {
    std::string error;
    auto dol = stage2(5, true, true);
    CtgpStage2Table table;
    EXPECT_TRUE(ctgp_stage2_table(dol.data(), dol.size(), table, error) == CtgpStage2Result::Ok);
    EXPECT_TRUE(table.table == rule_table(5));
    EXPECT_EQ(table.payload_address, kPayloadAt);
    EXPECT_TRUE(table.steps > 256 * 5 && table.steps < 2000);
    CtgpChannelCups cups;
    EXPECT_TRUE(ctgp_stage2_cups(dol.data(), dol.size(), table, cups, error) == CtgpStage2Result::Ok);
    EXPECT_EQ(cups.address, kCupsAt);
    const auto plain = channel_program(true);
    EXPECT_EQ(cups.inflated, static_cast<u32>(plain.size()));
    EXPECT_TRUE(std::memcmp(cups.entries.data(), plain.data() + 0x200, cups.entries.size()) == 0);
    EXPECT_EQ(cups.entries[3], 2);
    EXPECT_EQ(cups.entries[4], 0x20);

    // Refused: no unpacking loop, a table that is not one-to-one (an even
    // step repeats values), damaged packed bytes, no settings step.
    dol = stage2(5, false, true);
    EXPECT_TRUE(ctgp_stage2_table(dol.data(), dol.size(), table, error) == CtgpStage2Result::Unsupported);
    EXPECT_TRUE(error.find("unpacking loop") != std::string::npos);
    dol = stage2(4, true, true);
    EXPECT_TRUE(ctgp_stage2_table(dol.data(), dol.size(), table, error) == CtgpStage2Result::Unsupported);
    EXPECT_TRUE(error.find("one-to-one") != std::string::npos);
    dol = stage2(5, true, true, true);
    EXPECT_TRUE(ctgp_stage2_table(dol.data(), dol.size(), table, error) == CtgpStage2Result::Ok);
    EXPECT_TRUE(ctgp_stage2_cups(dol.data(), dol.size(), table, cups, error) != CtgpStage2Result::Ok);
    dol = stage2(5, true, false);
    EXPECT_TRUE(ctgp_stage2_table(dol.data(), dol.size(), table, error) == CtgpStage2Result::Ok);
    EXPECT_TRUE(ctgp_stage2_cups(dol.data(), dol.size(), table, cups, error) == CtgpStage2Result::Unsupported);
    EXPECT_TRUE(error.find("no cup-layout table") != std::string::npos);
    // a payload that does not fit the file
    dol = stage2(5, true, true);
    EXPECT_TRUE(ctgp_stage2_table(dol.data(), dol.size(), table, error) == CtgpStage2Result::Ok);
    CtgpStage2Table wrong = table;
    wrong.payload_length = static_cast<u32>(dol.size());
    EXPECT_TRUE(ctgp_stage2_cups(dol.data(), dol.size(), wrong, cups, error) == CtgpStage2Result::Damaged);
}

int main() {
    test_cache_and_breakpoint_registers();
    test_refusals();
    test_decode();
    if (g_failures) {
        std::cerr << g_failures << " ctgpstage2 test(s) failed" << std::endl;
        return 1;
    }
    std::cout << "ctgpstage2 tests passed" << std::endl;
    return 0;
}
