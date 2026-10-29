// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/gcfont.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << '\n'; ++g_failures; } } while (0)
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

namespace {

constexpr std::uint32_t kBase = 0x80200000;
constexpr std::uint16_t kCache = 0x9000;
constexpr std::uint32_t kBlr = (19u << 26) | (20u << 21) | (16u << 1);

std::uint32_t d_form(unsigned op, unsigned destination, unsigned base, unsigned value) {
    return (op << 26) | (destination << 21) | (base << 16) | value;
}

std::uint32_t jump(unsigned displacement) { return (18u << 26) | displacement; }

std::uint32_t conditional(unsigned bo, unsigned bit, unsigned displacement) {
    return (16u << 26) | (bo << 21) | (bit << 16) | displacement;
}

std::uint32_t compare(unsigned op, unsigned source, unsigned value, unsigned field = 0) {
    return d_form(op, field << 2, source, value);
}

struct Fixture {
    std::vector<std::uint32_t> words;
    std::vector<std::uint8_t> bytes;
    std::size_t start = 0;
    unsigned result = 3;

    std::vector<riftwii::CodeRange> ranges() {
        bytes.clear();
        for (std::uint32_t word : words) {
            bytes.push_back(static_cast<std::uint8_t>(word >> 24));
            bytes.push_back(static_cast<std::uint8_t>(word >> 16));
            bytes.push_back(static_cast<std::uint8_t>(word >> 8));
            bytes.push_back(static_cast<std::uint8_t>(word));
        }
        return {{kBase, bytes.data(), bytes.size()}};
    }

    std::vector<riftwii::GcFontPatch> patches(const std::string& id = "TEST01") {
        return riftwii::find_gc_font_patches(ranges(), id);
    }
};

// Synthetic instructions assembled from fields. The common decision is
// shared by all three inspected games; inlining schedules font bookkeeping
// within its cache guard in Giftpia and Kururin Squash.
Fixture make_fixture(bool inlined = false, bool scheduled = false, unsigned base = 4,
                     unsigned strap = 0, unsigned encoding = 0, unsigned tested = 0,
                     unsigned field = 0, bool and_test = false, unsigned vi = 0xCC00) {
    Fixture text;
    auto& words = text.words;
    const unsigned cached = inlined ? 9 : 3;
    text.result = cached;
    words.push_back(d_form(40, cached, 13, kCache));
    if (scheduled) words.push_back(d_form(40, 10, 8, 0x22));
    words.push_back(compare(10, cached, 1, field));
    if (scheduled) {
        words.push_back(d_form(36, 10, 8, 0x14));
        words.push_back(d_form(14, 11, 11, 1));
        words.push_back((31u << 26) | (10u << 21) | (11u << 16) | (12u << 11) | (235u << 1));
    }
    if (inlined) {
        words.push_back(conditional(12, field * 4 + 1, 8));
        words.push_back(jump(19 * 4));
    } else {
        words.push_back((19u << 26) | (4u << 21) | ((field * 4 + 1) << 16) | (16u << 1));
    }
    text.start = words.size();
    words.push_back(d_form(15, base, 0, 0x8000));
    words.push_back(d_form(32, strap, base, 0xCC));
    words.push_back(compare(11, strap, 0, field));
    words.push_back(conditional(12, field * 4 + 2, 12));
    words.push_back(conditional(12, field * 4, 44));
    words.push_back(jump(40));
    words.push_back(d_form(15, base, 0, vi));
    words.push_back(d_form(40, strap, base, 0x206E));
    if (and_test) {
        words.push_back(d_form(28, strap, tested, 2));
    } else {
        words.push_back(d_form(21, strap, tested, (30u << 6) | (30u << 1) | 1u));
    }
    words.push_back(conditional(12, 2, 12));
    words.push_back(d_form(14, encoding, 0, 1));
    words.push_back(jump(8));
    words.push_back(d_form(14, encoding, 0, 0));
    words.push_back(d_form(44, encoding, 13, kCache));
    words.push_back(jump(12));
    words.push_back(d_form(14, encoding, 0, 0));
    words.push_back(d_form(44, encoding, 13, kCache));
    words.push_back(d_form(40, inlined ? 9 : 3, 13, kCache));
    words.push_back(kBlr);
    return text;
}

void test_shapes() {
    for (bool inlined : {false, true}) {
        for (bool scheduled : {false, true}) {
            if (!inlined && scheduled) continue;
            for (unsigned vi : {0xCC00u, 0xCD00u}) {
                for (bool and_test : {false, true}) {
                    Fixture text = make_fixture(inlined, scheduled, 6, 7, 7, 7, 3, and_test, vi);
                    const auto before = text.words;
                    const auto found = text.patches("GGFJ01");
                    EXPECT_EQ(found.size(), 1u);
                    if (found.size() == 1) {
                        EXPECT_EQ(found[0].address, kBase + 4 * (text.start + 7));
                        EXPECT_EQ(found[0].instruction, d_form(14, 7, 0, 2));
                    }
                    EXPECT_EQ(text.words, before);
                }
            }
        }
    }
    Fixture text = make_fixture();
    for (const std::string id : {"GGFJ01", "GKQJ01"}) EXPECT_EQ(text.patches(id).size(), 1u);
    for (const std::string id : {"", "J", "GGF", "GMPE01", "GMPP01", "GGFj01"}) {
        EXPECT_TRUE(text.patches(id).empty());
    }
    Fixture second = make_fixture(true, true);
    text.words.insert(text.words.end(), second.words.begin(), second.words.end());
    EXPECT_EQ(text.patches("GGFJ01").size(), 2u);
}

void test_refusals() {
    // The TV and cache layout are no longer requirements. The VI address,
    // exclusive use of the strap value and a complete bit test still are.
    const std::vector<std::pair<unsigned, std::uint32_t>> mutations = {
        {6, d_form(15, 4, 0, 0xCC01)},
        {7, d_form(40, 0, 5, 0x206E)},
        {7, d_form(40, 0, 4, 0x206C)},
        {7, d_form(14, 0, 0, 1)},  // a 480p-style replacement is not a font load
        {8, d_form(21, 0, 0, (30u << 6) | (31u << 1))},  // VIGetDTVStatus
        {8, d_form(21, 0, 0, (30u << 6) | (30u << 1))},  // no CR update
        {8, d_form(28, 1, 0, 2)},
        {8, d_form(28, 0, 1, 2)},  // a different result could stay live after the decision
        {8, d_form(28, 0, 0, 1)},
        {9, conditional(12, 6, 12)},  // consumes another condition field
    };
    for (const auto& mutation : mutations) {
        Fixture text = make_fixture();
        text.words[text.start + mutation.first] = mutation.second;
        EXPECT_TRUE(text.patches("GGFJ01").empty());
    }
    Fixture complete = make_fixture();
    // A complete local bit test is enough; later cache stores may differ.
    for (std::size_t size = 0; size < complete.start + 10; ++size) {
        Fixture truncated = complete;
        truncated.words.resize(size);
        EXPECT_TRUE(truncated.patches("GGFJ01").empty());
    }
    auto ranges = complete.ranges();
    ranges[0].size = (complete.start + 10) * 4 - 1;
    EXPECT_TRUE(riftwii::find_gc_font_patches(ranges, "GGFJ01").empty());
    ranges[0].bytes = nullptr;
    EXPECT_TRUE(riftwii::find_gc_font_patches(ranges, "GGFJ01").empty());
    ranges = complete.ranges();
    const std::size_t split = (complete.start + 8) * 4;
    ranges.push_back({kBase + static_cast<std::uint32_t>(split), complete.bytes.data() + split,
                      complete.bytes.size() - split});
    ranges[0].size = split;
    EXPECT_TRUE(riftwii::find_gc_font_patches(ranges, "GGFJ01").empty());
    EXPECT_TRUE(riftwii::find_gc_font_patches({}, "GGFJ01").empty());
}

std::uint32_t rotate(unsigned source, unsigned destination, unsigned shift,
                     unsigned first, unsigned last, bool record) {
    return d_form(21, source, destination,
                  (shift << 11) | (first << 6) | (last << 1) | (record ? 1u : 0u));
}

void expect_patch(Fixture text, std::size_t load, unsigned destination) {
    std::vector<riftwii::GcFontRead> unclassified;
    const auto patches = riftwii::find_gc_font_patches(text.ranges(), "GRWJD9", &unclassified);
    EXPECT_EQ(patches.size(), 1u);
    EXPECT_TRUE(unclassified.empty());
    if (patches.size() == 1) {
        EXPECT_EQ(patches[0].address, kBase + load * 4);
        EXPECT_EQ(patches[0].instruction, d_form(14, destination, 0, 2));
    }
    EXPECT_TRUE(text.patches("GRWED9").empty());
}

void test_wider_layouts() {
    // No cached getter or TV-format prelude; a different compiler schedules
    // unrelated work between the VI base, read, mask and branch.
    Fixture direct;
    direct.words = {
        d_form(15, 8, 0, 0xCC00),
        d_form(14, 10, 0, 7),
        d_form(40, 5, 8, 0x206E),
        d_form(36, 10, 1, 8),
        d_form(28, 5, 5, 2),
        d_form(14, 9, 0, 1),
        conditional(4, 2, 8),
        kBlr,
    };
    expect_patch(direct, 2, 5);

    // A different guard, relocated VI, and an offset built into another
    // base register. A non-recording mask uses an explicit cmpwi in CR3.
    Fixture offset;
    offset.words = {
        d_form(32, 3, 13, kCache),
        compare(11, 3, 0),
        conditional(12, 2, 28),
        d_form(15, 11, 0, 0xCD00),
        d_form(14, 12, 11, 0x2000),
        d_form(40, 6, 12, 0x6E),
        rotate(6, 6, 0, 30, 30, false),
        compare(11, 6, 0, 3),
        conditional(12, 14, 8),
        kBlr,
    };
    expect_patch(offset, 5, 6);

    // extrwi r7,r7,1,30 is rlwinm r7,r7,31,31,31. The
    // entire address may also be constructed with ori before lhz ...,0.
    Fixture extracted;
    extracted.words = {
        d_form(15, 4, 0, 0xCC00),
        d_form(24, 4, 9, 0x206E),
        d_form(40, 7, 9, 0),
        rotate(7, 7, 31, 31, 31, true),
        conditional(12, 2, 8),
        kBlr,
    };
    expect_patch(extracted, 2, 7);
    extracted.words[3] = rotate(7, 7, 31, 31, 31, false);
    extracted.words.insert(extracted.words.begin() + 4, compare(11, 7, 0));
    expect_patch(extracted, 2, 7);

    // Signed load offsets and r0 as a logical source follow PowerPC's
    // actual address rules, rather than matching only the literal 0x206E.
    Fixture signed_offset;
    signed_offset.words = {
        d_form(15, 0, 0, 0xCC00),
        d_form(24, 0, 4, 0x3000),
        d_form(40, 7, 4, 0xF06E),
        rotate(7, 7, 0, 30, 30, true),
        conditional(12, 2, 8),
    };
    expect_patch(signed_offset, 2, 7);
}

void test_uncertain_reads() {
    const std::vector<std::vector<std::uint32_t>> uses = {
        {d_form(28, 7, 7, 1), conditional(12, 2, 8)},  // component cable
        {rotate(7, 7, 0, 31, 31, true), conditional(12, 2, 8)},
        {rotate(7, 7, 0, 30, 31, true), conditional(12, 2, 8)},  // both bits
        {rotate(7, 7, 1, 30, 30, true), conditional(12, 2, 8)},  // wrong source bit
        {d_form(28, 7, 6, 2), conditional(12, 2, 8)},  // raw r7 stays live
        {d_form(36, 7, 1, 8), d_form(28, 7, 7, 2), conditional(12, 2, 8)},
        {d_form(14, 8, 7, 0), d_form(28, 7, 7, 2), conditional(12, 2, 8)},
        {jump(4), d_form(28, 7, 7, 2), conditional(12, 2, 8)},
        {d_form(28, 7, 7, 2), compare(11, 8, 0), conditional(12, 2, 8)},
        {rotate(7, 7, 0, 30, 30, false), conditional(12, 2, 8)},
        {d_form(28, 7, 7, 2)},  // truncated before the branch
        {},
    };
    for (const auto& use : uses) {
        Fixture text;
        text.words = {d_form(15, 4, 0, 0xCC00), d_form(40, 7, 4, 0x206E)};
        text.words.insert(text.words.end(), use.begin(), use.end());
        std::vector<riftwii::GcFontRead> reads;
        EXPECT_TRUE(riftwii::find_gc_font_patches(text.ranges(), "GRWJD9", &reads).empty());
        EXPECT_EQ(reads.size(), 1u);
        if (reads.size() == 1) {
            EXPECT_EQ(reads[0].address, kBase + 4);
            EXPECT_EQ(reads[0].word_count, std::min<std::size_t>(5, text.words.size() - 1));
            for (unsigned index = 0; index < reads[0].word_count; ++index) {
                EXPECT_EQ(reads[0].words[index], text.words[index + 1]);
            }
        }
    }
    // A clobbered, unknown or non-VI base is not a proven DTV read.
    for (std::uint32_t setup : {d_form(15, 4, 0, 0x8000), d_form(32, 4, 13, 0),
                                d_form(14, 4, 0, 0), jump(4)}) {
        Fixture text;
        text.words = {d_form(15, 4, 0, 0xCC00), setup, d_form(40, 7, 4, 0x206E),
                      d_form(28, 7, 7, 2), conditional(12, 2, 8)};
        std::vector<riftwii::GcFontRead> reads;
        EXPECT_TRUE(riftwii::find_gc_font_patches(text.ranges(), "GRWJD9", &reads).empty());
        EXPECT_TRUE(reads.empty());
    }
}

// Execute the small synthetic getter to check behavior, including branch
// paths and cached returns. This is deliberately limited to its instructions.
unsigned run_getter(Fixture text, bool patched, unsigned tv, unsigned strap, unsigned cache) {
    if (patched) {
        for (const auto& patch : text.patches("GGFJ01")) {
            text.words[(patch.address - kBase) / 4] = patch.instruction;
        }
    }
    std::array<std::uint32_t, 32> registers{};
    std::array<bool, 32> conditions{};
    registers[13] = 0x80400000;
    const std::uint32_t cache_address = registers[13] + static_cast<std::uint32_t>(static_cast<std::int16_t>(kCache));
    for (std::size_t pc = 0, steps = 0; steps < 64 && pc < text.words.size(); ++steps) {
        const std::uint32_t word = text.words[pc];
        const unsigned op = word >> 26;
        const unsigned rd = (word >> 21) & 31;
        const unsigned ra = (word >> 16) & 31;
        const unsigned low = word & 0xFFFF;
        const auto signed_low = static_cast<std::int16_t>(low);
        std::size_t next = pc + 1;
        if (op == 14 || op == 15) {
            const std::uint32_t value = static_cast<std::uint32_t>(signed_low);
            registers[rd] = (ra == 0 ? 0 : registers[ra]) + (op == 15 ? value << 16 : value);
        } else if (op == 32 || op == 40 || op == 44) {
            const std::uint32_t address = registers[ra] + static_cast<std::uint32_t>(signed_low);
            if (op == 44) {
                EXPECT_EQ(address, cache_address);
                cache = registers[rd] & 0xFFFF;
            } else if (address == cache_address) {
                registers[rd] = cache;
            } else if (address == 0x800000CC) {
                registers[rd] = tv;
            } else {
                EXPECT_TRUE(address == 0xCC00206E || address == 0xCD00206E);
                registers[rd] = strap;
            }
        } else if (op == 10 || op == 11) {
            const unsigned condition = ((word >> 23) & 7) * 4;
            std::int64_t lhs = registers[ra];
            std::int64_t rhs = low;
            if (op == 11) {
                lhs = static_cast<std::int32_t>(registers[ra]);
                rhs = signed_low;
            }
            conditions[condition] = lhs < rhs;
            conditions[condition + 1] = lhs > rhs;
            conditions[condition + 2] = lhs == rhs;
        } else if (op == 21 || op == 28) {
            registers[ra] = registers[rd] & 2;
            conditions[2] = registers[ra] == 0;
        } else if (op == 16 || op == 19) {
            const bool taken = rd == 20 || conditions[ra] == (rd == 12);
            if (taken) {
                if (op == 19) return registers[text.result];
                next = pc + signed_low / 4;
            }
        } else if (op == 18) {
            next = pc + (word & 0x03FFFFFC) / 4;
        } else {
            EXPECT_TRUE(false);
            return 0xFFFF;
        }
        pc = next;
    }
    EXPECT_TRUE(false);
    return 0xFFFF;
}

void check_getter_behavior(const Fixture& text) {
    for (unsigned tv : {0u, 1u, 2u, 3u, 4u, 5u, 99u, 0xFFFFFFFFu}) {
        for (unsigned strap : {0u, 1u, 2u, 3u}) {
            for (unsigned cache : {0u, 1u, 0xFFFFu}) {
                unsigned original = cache;
                unsigned expected = cache;
                if (cache > 1) {
                    original = tv == 0 && (strap & 2) ? 1 : 0;
                    expected = tv == 0 ? 1 : 0;
                }
                EXPECT_EQ(run_getter(text, false, tv, strap, cache), original);
                EXPECT_EQ(run_getter(text, true, tv, strap, cache), expected);
            }
        }
    }
}

void test_behavior() {
    for (bool and_test : {false, true}) {
        for (bool inlined : {false, true}) {
            check_getter_behavior(make_fixture(inlined, false, 6, 7, 7, 7, 3, and_test));
        }
    }
}

}  // namespace

int main() {
    test_shapes();
    test_refusals();
    test_wider_layouts();
    test_uncertain_reads();
    test_behavior();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "GameCube font encoding tests passed\n";
    return 0;
}
