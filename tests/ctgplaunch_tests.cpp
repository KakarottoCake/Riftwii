// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The CTGP-R launch plan and its placement (riftwii/ctgplaunch.hpp), on made-up data.
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgpfat.hpp"
#include "riftwii/ctgpapply.hpp"
#include "riftwii/ctgplaunch.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << std::hex << (a) << " != " << (b) << std::dec << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

template <typename Bytes>
static void set(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = v >> 24; b[at + 1] = v >> 16; b[at + 2] = v >> 8; b[at + 3] = v;
}
template <typename Bytes>
static std::uint32_t get(const Bytes& b, std::size_t at) {
    return (std::uint32_t(b[at]) << 24) | (std::uint32_t(b[at + 1]) << 16) | (std::uint32_t(b[at + 2]) << 8) | b[at + 3];
}

static std::vector<std::uint8_t> fat_sector(unsigned spc, unsigned reserved, unsigned fats, std::uint32_t sectors_per_fat,
                                            std::uint32_t total, const char* label) {
    std::vector<std::uint8_t> s(512, 0);
    s[0x0B] = 0; s[0x0C] = 2; s[0x0D] = spc; s[0x0E] = reserved; s[0x10] = fats;
    set(s, 0x20, 0);
    s[0x20] = total; s[0x21] = total >> 8; s[0x22] = total >> 16; s[0x23] = total >> 24;
    s[0x24] = sectors_per_fat; s[0x25] = sectors_per_fat >> 8; s[0x26] = sectors_per_fat >> 16;
    s[0x2C] = 2;
    std::memcpy(&s[0x47], label, 11);
    std::memcpy(&s[0x52], "FAT32   ", 8);
    s[0x1FE] = 0x55; s[0x1FF] = 0xAA;
    return s;
}

static CtgpInputs inputs() {
    CtgpInputs in;
    in.boot_bin.assign(0x121CB4, 0);
    in.boat_bin.assign(0x5A3D8, 0);
    for (std::uint32_t k = 0; k < 0x30; ++k) set(in.boot_bin, 0x2814 + 4 * k, kCtgpBoatBase + 0x100 + 4 * k);
    in.beat_bin = {0, 0, 0xCB, 0, 0, 0, 0, 0};
    // rows: game functions in 0x8000_4000.. for each region column
    in.rows_bin = {'C', 'R', 'T', '0', 0, 0, 0x0A, 0x0A};
    in.rows_bin.resize(8 + 2570 * 12);
    for (std::uint32_t r = 0; r < 2570; ++r)
        for (std::uint32_t c = 0; c < 3; ++c) set(in.rows_bin, 8 + (r * 3 + c) * 4, 0x80004000 + 0x200 * r + 0x4000000 * 0 + 0x40 * c);
    // the 54 words before the rows: addresses the entry routine patches
    for (std::uint32_t i = 0; i < 54; ++i) {
        const std::size_t at = in.rows_bin.size();
        in.rows_bin.resize(at + 4);
        set(in.rows_bin, at, 0x80010000 + 0x1000 * (i / 3) + 0x10 * (i % 3));
    }
    // packages: three entries, CORE and CTGPR at the tested versions
    in.packages_bin = {'P', 'A', 'K', '0', 0, 0, 0, 0x40, 0, 0, 0, 0, 0, 0, 0, 3};
    auto add = [&](const char* name, std::uint32_t version) {
        std::vector<std::uint8_t> rec(16, 0);
        std::memcpy(rec.data(), name, std::strlen(name));
        set(rec, 8, version);
        set(rec, 12, 0x0A000000);
        in.packages_bin.insert(in.packages_bin.end(), rec.begin(), rec.end());
    };
    add("CORE", 0x01030412);
    add("CTGPR", 0x01031196);
    add("CHAN", 0x00020001);
    in.efa_boot_sector = fat_sector(8, 8, 1, 4096, 0x400000, "CTGP-EFA000");
    in.sda_boot_sector = fat_sector(64, 32, 2, 8192, 8388608, "SDCARD     ");
    in.sda_volume_lba = 2048;
    in.config_ini = "[ctgpr]\nctgpr = yes\nlanmode = yes\nroom_race_count = 3\ncup_course_layout_using = 1\n";
    // The course table and cup layouts are baked into the build (ctgpapply.cpp),
    // so nothing is set on in.tables here beyond the package-disk keys.
    in.region = CtgpRegion::P;
    return in;
}

static void test_assemble() {
    const CtgpInputs in = inputs();
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    EXPECT_EQ(plan.image_end, kCtgpBoatBase + 0x5A3D8u);
    // partition objects in the BOOT image
    const std::size_t sda = kCtgpSdaBase - kCtgpBootBase, efa = kCtgpEfaBase - kCtgpBootBase;
    EXPECT_EQ(get(plan.boot, sda + ctgp_part::disc), kCtgpSdaDisc);
    EXPECT_EQ(get(plan.boot, sda + ctgp_part::fat_start), 2048u + 32);
    EXPECT_EQ(get(plan.boot, efa + ctgp_part::disc), kCtgpEfaDisc);
    EXPECT_EQ(get(plan.boot, efa + ctgp_part::fat_start), 8u);
    EXPECT_EQ(plan.boot[efa + ctgp_part::read_only], 0);   // features 3 includes can-write, as the channel's
    EXPECT_EQ(plan.boot[sda + ctgp_part::read_only], 0);
    // loader words
    EXPECT_EQ(get(plan.boot, 0x816ffa2c - kCtgpBootBase), 12u);      // player_count_offline default
    EXPECT_EQ(get(plan.boot, 0x816ff9dc - kCtgpBootBase), 0x01031196u);   // CTGPR id
    EXPECT_EQ(get(plan.boot, 0x816ff518 - kCtgpBootBase), 1u);       // ctgpr = yes
    EXPECT_EQ(get(plan.boot, 0x816ffed8 - kCtgpBootBase), 62u);      // layout 1: 54 groups + 8
    EXPECT_EQ(get(plan.boot, 0x816ff9f4 - kCtgpBootBase), 0u);       // lanmode = yes in config.ini, still off
    EXPECT_EQ(get(plan.boot, 0x816b5328 - kCtgpBootBase), 0u);       // no network settings handed over
    // the unusable signing key is in place, nothing names a card's key
    EXPECT_EQ(get(plan.boot, 0x816ff538 - kCtgpBootBase), 0x816ff570u);
    EXPECT_EQ(get(plan.boot, 0x816ff5c8 - kCtgpBootBase), 5u);       // x: 5 words (the fifth BIGNUM's dmax)
    EXPECT_EQ(get(plan.boot, 0x816ff4f0 - kCtgpBootBase), 0u);
    EXPECT_EQ(get(plan.boot, 0x816ff4e8 - kCtgpBootBase), 0u);
    // low memory and hooks are game operations
    bool low = false, hook = false;
    for (const CtgpGameOp& op : plan.game_ops) {
        if (op.address == 0x80000a46 && op.bytes.size() == 1 && op.bytes[0] == 3) low = true;
        if (op.address == 0x80004000 + 0x80 && op.bytes.size() == 4 && (op.bytes[0] >> 2) == 0x12) hook = true;  // row 0, PAL column, a branch
    }
    EXPECT_TRUE(low);
    EXPECT_TRUE(hook);
    // bad inputs
    CtgpInputs bad = in;
    bad.sda_boot_sector.assign(512, 0);
    EXPECT_TRUE(!ctgp_assemble(bad, plan, error));
    bad = in;
    bad.packages_bin[0] = 'X';
    EXPECT_TRUE(!ctgp_assemble(bad, plan, error));
}

// Both the tested core and the tested CTGPR track package are required: the cup
// layouts are baked for one CTGPR version, so another track pack is refused too.
static void test_version_guard() {
    CtgpLaunchPlan plan;
    std::string error;
    CtgpInputs in = inputs();
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    // a different core is refused
    CtgpInputs other = in;
    set(other.packages_bin, 16 + 8, 0x01030413);
    EXPECT_TRUE(!ctgp_assemble(other, plan, error));
    EXPECT_TRUE(error.find("different version") != std::string::npos);
    EXPECT_TRUE(error.find("0x01030413") != std::string::npos);
    // a different CTGPR track package is refused now that the cups are baked
    other = in;
    set(other.packages_bin, 16 + 16 + 8, 0x01031197);   // CTGPR is the second record
    EXPECT_TRUE(!ctgp_assemble(other, plan, error));
    EXPECT_TRUE(error.find("0x01031197") != std::string::npos);
    // no CORE at all is refused
    other = in;
    std::memcpy(&other.packages_bin[16], "XORE", 4);
    EXPECT_TRUE(!ctgp_assemble(other, plan, error));
    // the tested core and the tested CTGPR together are accepted
    std::vector<CtgpPackage> packages = {{"CORE", kCtgpTestedCoreVersion, 0}, {"CTGPR", kCtgpTestedCtgprVersion, 0}};
    EXPECT_TRUE(ctgp_check_versions(packages, error));
    packages[1].version = 0x09999999;  // a different track pack: refused
    EXPECT_TRUE(!ctgp_check_versions(packages, error));
    packages[1].version = kCtgpTestedCtgprVersion;
    packages[0].version = 0;  // a different core: refused
    EXPECT_TRUE(!ctgp_check_versions(packages, error));
}

static void test_channel_stub() {
    CtgpLaunchPlan plan;
    std::string error;
    CtgpInputs in = inputs();
    in.channel_stub = "sda:/apps/riftwii/boot.dol";
    // BOOT.BIN without the stub's path there: refused
    EXPECT_TRUE(!ctgp_assemble(in, plan, error));
    const std::size_t at = kCtgpChannelStub - kCtgpBootBase;
    std::memcpy(&in.boot_bin[at], "efa:/packages/CHAN/stub.dol", 28);
    in.boot_bin[at + 28] = 0x5A;
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    EXPECT_TRUE(std::memcmp(&plan.boot[at], "sda:/apps/riftwii/boot.dol\0\0", 28) == 0);
    EXPECT_TRUE(plan.boot[at + 28] == 0x5A);
    // no path: the channel's stub stays
    in.channel_stub.clear();
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    EXPECT_TRUE(std::memcmp(&plan.boot[at], "efa:/packages/CHAN/stub.dol", 28) == 0);
    // too long for the 28 bytes
    in.channel_stub = "sda:/apps/riftwii-long-name/boot.dol";
    EXPECT_TRUE(!ctgp_assemble(in, plan, error));
    // A BOOT.BIN too short to hold the stub's fixed-offset path is refused
    // before anything reads or writes past its end (#3). The partition
    // objects need the image out to 0xc5c00; this is longer than that but a
    // few bytes shorter than the stub's span, so the guard is what stops it.
    CtgpInputs short_boot = inputs();
    short_boot.channel_stub = "sda:/apps/riftwii/boot.dol";
    short_boot.boot_bin.resize((kCtgpChannelStub - kCtgpBootBase) + kCtgpChannelStubLength - 4, 0);
    EXPECT_TRUE(!ctgp_assemble(short_boot, plan, error));
    EXPECT_TRUE(error.find("too small to hold the channel stub") != std::string::npos);
}

static CtgpMemoryState good_state() {
    CtgpMemoryState s;
    s.loader_end = 0x80900000;
    s.apploader_end = 0x81300000;
    s.fst_address = 0x817E0000;
    s.bi2_address = 0x817FE000;
    s.loaded = {{0x80004000, 0x200000}, {0x80300000, 0x100000}};
    return s;
}

static void test_layout() {
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(inputs(), plan, error));
    CtgpMemoryState s = good_state();
    EXPECT_TRUE(ctgp_check_layout(plan, s, error));
    s = good_state(); s.loader_end = 0x81610000;
    EXPECT_TRUE(!ctgp_check_layout(plan, s, error));
    s = good_state(); s.apploader_end = 0x81700000;
    EXPECT_TRUE(!ctgp_check_layout(plan, s, error));
    s = good_state(); s.fst_address = 0x81700000;
    EXPECT_TRUE(!ctgp_check_layout(plan, s, error));
    s = good_state(); s.bi2_address = 0x817B0000;
    EXPECT_TRUE(!ctgp_check_layout(plan, s, error));
    s = good_state(); s.loaded.push_back({0x81650000, 0x1000});
    EXPECT_TRUE(!ctgp_check_layout(plan, s, error));
}

// The placed image must fit MEM1: BOOT below BOAT, BOAT below MEM1's top (#4).
static void test_size_caps() {
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(inputs(), plan, error));
    CtgpMemoryState s = good_state();
    EXPECT_TRUE(ctgp_check_layout(plan, s, error));
    // BOAT ending past the top of MEM1 is refused.
    CtgpLaunchPlan over = plan;
    over.image_end = kCtgpMem1End + 4;
    EXPECT_TRUE(!ctgp_check_layout(over, s, error));
    EXPECT_TRUE(error.find("past the end of MEM1") != std::string::npos);
    // BOOT running into BOAT is refused.
    CtgpLaunchPlan over_boot = plan;
    over_boot.boot.resize((kCtgpBoatBase - kCtgpBootBase) + 4, 0);
    EXPECT_TRUE(!ctgp_check_layout(over_boot, s, error));
    // The caps sit above the real file sizes, so a real image still fits.
    EXPECT_TRUE(0x121c34u < kCtgpBootMax && 0x5a418u < kCtgpBoatMax);
}

namespace {
struct FakeMemory final : MemoryAccess {
    std::vector<std::uint8_t> ram = std::vector<std::uint8_t>(0x1800000, 0);
    bool in(std::uint32_t a, std::size_t n) const { return a >= 0x80000000 && std::uint64_t(a) + n <= 0x81800000ull; }
    bool read(std::uint32_t a, std::uint8_t* out, std::size_t n) override {
        if (!in(a, n)) return false;
        std::memcpy(out, &ram[a - 0x80000000], n);
        return true;
    }
    bool write(std::uint32_t a, const std::uint8_t* b, std::size_t n) override {
        if (!in(a, n)) return false;
        std::memcpy(&ram[a - 0x80000000], b, n);
        return true;
    }
};
}  // namespace

static void test_place() {
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(inputs(), plan, error));
    FakeMemory mem;
    // game code the settings step moves: row 1253, PAL column
    const std::uint32_t row = 0x80004000 + 0x200 * 1253 + 0x80;
    for (int i = 0; i < 0x24; ++i) mem.ram[row + 0x0C - 0x80000000 + i] = static_cast<std::uint8_t>(0x40 + i);
    EXPECT_TRUE(ctgp_place(plan, mem, error));
    auto word = [&](std::uint32_t a) {
        return (std::uint32_t(mem.ram[a - 0x80000000]) << 24) | (std::uint32_t(mem.ram[a - 0x80000000 + 1]) << 16) |
               (std::uint32_t(mem.ram[a - 0x80000000 + 2]) << 8) | mem.ram[a - 0x80000000 + 3];
    };
    EXPECT_EQ(word(kCtgpBootBase + 0x2814), kCtgpBoatBase + 0x100);          // BOOT in place
    EXPECT_EQ(mem.ram[0x80000a46 - 0x80000000], 3);                           // low memory byte
    EXPECT_EQ(word(0x80004000 + 0x80) >> 26, 18u);                            // the hook branch on row 0
    // RowMove shifts 0x24 bytes from +0x0c to +0x14 (forward, overlapping), then the stores at +0x0c, +0x10
    // and +0x38 are made
    EXPECT_EQ(word(row + 0x0C), 0x28040049u);
    EXPECT_EQ(word(row + 0x10), 0x41810044u);
    EXPECT_EQ(mem.ram[row + 0x14 - 0x80000000], 0x40);              // the byte that was at +0x0c
    EXPECT_EQ(mem.ram[row + 0x14 - 0x80000000 + 0x20], 0x40 + 0x20);   // +0x34 holds the byte from +0x2c
}

// The plan's game operations: the 37 hooks and the entry routine's five writes.
static void test_operations() {
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(inputs(), plan, error));
    auto count = [](const CtgpLaunchPlan& p, CtgpGameOp::Group g) {
        std::size_t n = 0;
        for (const auto& op : p.game_ops) n += op.group == g;
        return n;
    };
    EXPECT_EQ(count(plan, CtgpGameOp::Hook), (std::size_t)37);
    EXPECT_EQ(count(plan, CtgpGameOp::Environment), (std::size_t)5);
    // the environment writes: PAL column of the 54 words (index % 3 == 2)
    bool blr = false, zero808 = false, zero1376 = false;
    for (const auto& op : plan.game_ops) {
        if (op.group != CtgpGameOp::Environment) continue;
        if (op.address == 0x80010000 + 0x10 * 2 && op.bytes.size() == 4 && op.bytes[0] == 0x4E) blr = true;
        if (op.address == 0x80010000 + 0x1000 * 2 + 0x20 && op.bytes.size() == 808) zero808 = true;
        if (op.address == 0x80010000 + 0x1000 * 3 + 0x20 && op.bytes.size() == 1376) zero1376 = true;
    }
    EXPECT_TRUE(blr);
    EXPECT_TRUE(zero808);
    EXPECT_TRUE(zero1376);
}

// The packages' side: records in the BOOT image, the partitions' open-file lists, the merged files.
static void test_attach_files() {
    CtgpInputs in = inputs();
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    // one package: /Scene/a.szs -> efa:/p/a (signed), /b.bin -> sda:/b
    const std::string tail = std::string("efa:/p/a") + '\0' + "sda:/b" + '\0';
    CtgpBytes fst;
    auto w32 = [&](std::uint32_t x) { for (int s = 24; s >= 0; s -= 8) fst.push_back(std::uint8_t(x >> s)); };
    // root, Scene, a.szs, b.bin; names "", "Scene", "a.szs", "b.bin"
    const std::string names = std::string("\0Scene\0a.szs\0b.bin\0", 20);
    const std::uint32_t strings = 4 * 12 + (std::uint32_t)names.size();
    w32(0x01000000u); w32(0); w32(4);
    w32(0x01000001u); w32(0); w32(3);
    w32(0x00000007u); w32(strings); w32(2);
    w32(0x0000000Du); w32(strings + 9); w32(0);
    fst.insert(fst.end(), names.begin(), names.end());
    fst.insert(fst.end(), tail.begin(), tail.end());
    auto resolver = [](const std::string& device, const std::string& path, CtgpSourceFile& out) {
        if (device == "efa" && path == "/p/a") { out.size = 100; out.first_cluster = 9; return true; }
        if (device == "sda" && path == "/b") { out.size = 0x30000; out.first_cluster = 77; return true; }
        return false;
    };
    std::array<std::uint8_t, 20> digest{};
    for (int i = 0; i < 20; ++i) digest[i] = std::uint8_t(0x40 + i);
    const CtgpHashList hashes = CtgpHashList::unverified_for_tests({{"/Scene/a.szs", digest}});
    EXPECT_TRUE(!ctgp_attach_files(plan, {fst}, {nullptr}, resolver, error));   // a signed file without a list
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    EXPECT_TRUE(ctgp_attach_files(plan, {fst}, {&hashes}, resolver, error));
    EXPECT_EQ(plan.files.size(), (std::size_t)2);
    auto word = [&](std::uint32_t address) {
        const std::size_t at = address - kCtgpBootBase;
        return (std::uint32_t(plan.boot[at]) << 24) | (std::uint32_t(plan.boot[at + 1]) << 16) |
               (std::uint32_t(plan.boot[at + 2]) << 8) | plan.boot[at + 3];
    };
    // the first record (pool 0, index 0) is a.szs on efa:, the second (pool 1) is b.bin on sda:
    EXPECT_EQ(word(0x81628CB8), 100u);
    EXPECT_EQ(word(0x81628CB8 + 0x3C), 0x816CD9A0u);
    EXPECT_EQ(word(0x81661180), 0x30000u);
    EXPECT_EQ(word(0x81661180 + 0x3C), 0x816B5740u);
    EXPECT_EQ(word(0x816CD9A0 + 0x48), 1u);
    EXPECT_EQ(word(0x816CD9A0 + 0x4C), 0x81628CB8u);
    EXPECT_EQ(word(0x816B5740 + 0x48), 1u);
    EXPECT_EQ(word(0x816B5740 + 0x4C), 0x81661180u);
    // a signed file keeps a SHA-1 state in its record
    EXPECT_EQ(word(0x81628CB8 + 0x50), 0x67452301u);
    EXPECT_EQ(word(0x81628CB8 + 0xB4), 0x40414243u);   // its expected digest
    EXPECT_EQ(plan.files[0].game_path, std::string("/Scene/a.szs"));
    EXPECT_EQ(plan.files[1].handle, 0x8C000000u);
    // a missing required file stops the launch plan
    CtgpLaunchPlan plan2;
    EXPECT_TRUE(ctgp_assemble(in, plan2, error));
    auto none = [](const std::string&, const std::string&, CtgpSourceFile&) { return false; };
    fst[12 * 2 + 8] = 0; fst[12 * 2 + 9] = 0; fst[12 * 2 + 10] = 0; fst[12 * 2 + 11] = 3;   // a.szs: required and signed
    EXPECT_TRUE(!ctgp_attach_files(plan2, {fst}, {&hashes}, none, error));
}

// The drive startups: the first hook goes through a stub that starts "sda:" and "efa:" once.
static void test_startup() {
    CtgpInputs in = inputs();
    set(in.boot_bin, 0x912C8, 0x8179E6C4);   // sda: startup
    set(in.boot_bin, 0x912E8, 0x8179F0C0);   // efa: startup
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    const CtgpGameOp* hook0 = nullptr;
    const CtgpGameOp* stub = nullptr;
    const CtgpGameOp* flag = nullptr;
    for (const CtgpGameOp& op : plan.game_ops) {
        if (op.group == CtgpGameOp::Hook && op.hook_index == 0) hook0 = &op;
        if (op.group == CtgpGameOp::Startup && op.bytes.size() > 4) stub = &op;
        if (op.group == CtgpGameOp::Startup && op.bytes.size() == 4) flag = &op;
    }
    EXPECT_TRUE(hook0 && stub && flag);
    if (!(hook0 && stub && flag)) return;
    EXPECT_EQ(stub->address, 0x80002E00u);
    EXPECT_EQ(flag->address, 0x80002FF0u);
    // the hook now branches to the stub
    const std::uint32_t hw = get(hook0->bytes, 0);
    EXPECT_EQ(hw >> 26, 18u);
    std::int32_t hli = (std::int32_t)(hw & 0x03FFFFFC);
    if (hli & 0x02000000) hli -= 0x04000000;
    EXPECT_EQ(hook0->address + (std::uint32_t)hli, 0x80002E00u);
    EXPECT_EQ(plan.hook0_original >> 26, 18u);
    // the stub starts both drives, then goes on to where the hook went
    bool sda = false, efa = false;
    for (std::size_t at = 0; at + 8 <= stub->bytes.size(); at += 4) {
        if (get(stub->bytes, at) == 0x3D808179u && get(stub->bytes, at + 4) == 0x618CE6C4u) sda = true;
        if (get(stub->bytes, at) == 0x3D808179u && get(stub->bytes, at + 4) == 0x618CF0C0u) efa = true;
    }
    EXPECT_TRUE(sda && efa);
    EXPECT_EQ(get(stub->bytes, 0), 0x2C050000u);   // only a read at a virtual handle (r5 < 0)
    const std::size_t last = stub->bytes.size() - 4;
    const std::uint32_t tail = get(stub->bytes, last);
    EXPECT_EQ(tail >> 26, 18u);
    std::int32_t li = (std::int32_t)(tail & 0x03FFFFFC);
    if (li & 0x02000000) li -= 0x04000000;
    std::int32_t orig = (std::int32_t)(plan.hook0_original & 0x03FFFFFC);
    if (orig & 0x02000000) orig -= 0x04000000;
    EXPECT_EQ(0x80002E00u + (std::uint32_t)last + (std::uint32_t)li, hook0->address + (std::uint32_t)orig);
    // the conditional branches land on that last word
    EXPECT_EQ((get(stub->bytes, 4) & 0xFFFC) , (std::uint32_t)last - 4);
    EXPECT_EQ((get(stub->bytes, 20) & 0xFFFC), (std::uint32_t)last - 20);
}

// The tracks' digests land in the loader's block in two parts; a short file is refused.
static void test_track_hashes() {
    CtgpInputs in = inputs();
    in.track_hashes.resize(255 * 20);
    for (std::size_t i = 0; i < in.track_hashes.size(); ++i) in.track_hashes[i] = static_cast<std::uint8_t>(i * 7 + 1);
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    const std::size_t a = kCtgpTrackHashesA - kCtgpBootBase, b = kCtgpTrackHashesB - kCtgpBootBase;
    EXPECT_EQ(plan.boot[a], in.track_hashes[0]);
    EXPECT_EQ(plan.boot[a + 0x27F], in.track_hashes[0x27F]);
    EXPECT_EQ(plan.boot[b], in.track_hashes[0x280]);
    EXPECT_EQ(plan.boot[b + (255 * 20 - 0x280) - 1], in.track_hashes[255 * 20 - 1]);
    EXPECT_EQ(plan.boot[b + 255 * 20 - 0x280], 0);
    in.track_hashes.resize(100);
    EXPECT_TRUE(!ctgp_assemble(in, plan, error));
}

// Ghosts may be saved unless My Stuff (on, not music only) replaces Common.szs.
static void test_common_flag() {
    auto word = [](const CtgpLaunchPlan& p, std::uint32_t a) { return get(p.boot, a - kCtgpBootBase); };
    CtgpInputs in = inputs();
    CtgpLaunchPlan plan;
    std::string error;
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    EXPECT_EQ(word(plan, 0x816FF508), 1u);
    EXPECT_EQ(word(plan, 0x816FF510), 0u);
    in.my_stuff_common = true;
    in.config_ini = "[ctgpr]\nmy_stuff = 1\n";
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    EXPECT_EQ(word(plan, 0x816FF508), 0u);
    EXPECT_EQ(word(plan, 0x816FF510), 1u);
    in.config_ini = "[ctgpr]\nmy_stuff = 3\n";
    EXPECT_TRUE(ctgp_assemble(in, plan, error));
    EXPECT_EQ(word(plan, 0x816FF508), 1u);
}

// The native-launch trigger is narrow: Mario Kart Wii, and exactly CTGP-R's
// own channel app, and nothing else (#2).
static void test_native_stand_in() {
    EXPECT_TRUE(ctgp_is_native_stand_in("RMCE01", "sd:/apps/ctgpr/boot.dol"));
    EXPECT_TRUE(ctgp_is_native_stand_in("RMCP01", "sd:/apps/ctgpr/boot.dol"));
    EXPECT_TRUE(ctgp_is_native_stand_in("RMCJ01", "sd:/apps/ctgpr/boot.dol"));
    EXPECT_TRUE(ctgp_is_native_stand_in("RMCK01", "sd:/apps/ctgpr/boot.dol"));
    // case does not matter in the path (the card is case-insensitive)
    EXPECT_TRUE(ctgp_is_native_stand_in("RMCE01", "sd:/apps/CTGPR/Boot.dol"));
    // another game, even with the same region letter, is not CTGP-R
    EXPECT_TRUE(!ctgp_is_native_stand_in("SMNE01", "sd:/apps/ctgpr/boot.dol"));
    EXPECT_TRUE(!ctgp_is_native_stand_in("RMGE01", "sd:/apps/ctgpr/boot.dol"));
    // another homebrew app on Mario Kart Wii is an ordinary pack
    EXPECT_TRUE(!ctgp_is_native_stand_in("RMCE01", "sd:/apps/other/boot.dol"));
    EXPECT_TRUE(!ctgp_is_native_stand_in("RMCE01", "sd:/apps/ctgpr/main.dol"));
    EXPECT_TRUE(!ctgp_is_native_stand_in("RMCE01", "sd:/apps/ctgpr/boot.dol.bak"));
    EXPECT_TRUE(!ctgp_is_native_stand_in("RMCE01", ""));
    // a bare prefix with no region letter is not a disc ID
    EXPECT_TRUE(!ctgp_is_native_stand_in("RMC", "sd:/apps/ctgpr/boot.dol"));
}

int main() {
    test_native_stand_in();
    test_common_flag();
    test_track_hashes();
    test_startup();
    test_attach_files();
    test_operations();
    test_version_guard();
    test_assemble();
    test_channel_stub();
    test_layout();
    test_size_caps();
    test_place();
    if (g_failures == 0) std::cout << "ctgplaunch tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
