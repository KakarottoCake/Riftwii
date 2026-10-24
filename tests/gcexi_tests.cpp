// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Invented instruction sequences exercising the EXI finder's state and
// call relationships. No game executable bytes are stored in these tests.
#include "riftwii/gcexi.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "gc_card.h"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kBase = 0x80004000;
constexpr std::uint32_t kBlocks = 0x80301000;
constexpr std::uint32_t kOtherBlocks = 0x80402000;
constexpr std::uint32_t kDecoyBlocks = 0x80503000;
constexpr std::uint32_t kNoTable = 0;
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kNop = 0x60000000;
constexpr std::uint32_t kProbeTimes = 0x386330C0;  // addi r3,r3,0x30C0
constexpr std::uint32_t kMinusOne = 0x3860FFFF;    // li r3,-1

std::uint32_t branch_link(std::uint32_t from, std::uint32_t to) {
    return 0x48000001u | ((to - from) & 0x03FFFFFC);
}

std::uint32_t clear_bit(unsigned bit) {
    const unsigned first = (32 - bit) % 32;
    const unsigned last = (30 - bit) % 32;
    return (21u << 26) | (first << 6) | (last << 1);
}

struct Text {
    std::vector<std::uint32_t> words{kBlr};
    std::vector<std::uint8_t> bytes;

    // A function of four prologue words, `body`, nops to `size` and a blr.
    // With a table, the prologue turns the channel into a block offset
    // (by 0x40 or, for the 2001 SDK, by 0x38) and loads the table's
    // address; without one it is a function no table lists.
    std::uint32_t add_function(bool old_sdk, std::uint32_t table, const std::vector<std::uint32_t>& body,
                               std::uint32_t size = 0x60) {
        const auto start = kBase + 4 * static_cast<std::uint32_t>(words.size());
        words.push_back(0x7C0802A6);  // mflr r0
        if (table == kNoTable) {
            words.insert(words.end(), {kNop, kNop, kNop});
        } else {
            words.push_back(old_sdk ? 0x1C030038 : 0x54033032);  // channel times the block size
            words.push_back(0x3C800000 | (table >> 16));          // lis r4,table@h
            words.push_back(0x38840000 | (table & 0xFFFF));       // addi r4,r4,table@l (below 0x8000 here)
        }
        words.insert(words.end(), body.begin(), body.end());
        // Keep base discovery inside this function and probes below 0x100.
        while (kBase + 4 * words.size() < start + size - 4) words.push_back(kNop);
        words.push_back(kBlr);
        return start;
    }

    void put(std::uint32_t address, std::uint32_t word) {
        words.at((address - kBase) / 4) = word;
    }

    void call(std::uint32_t function, unsigned body_index, std::uint32_t target) {
        const std::uint32_t address = function + 16 + 4 * body_index;
        put(address, branch_link(address, target));
    }

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
};

// How one invented SDK is linked.
struct SdkShape {
    bool old_sdk = false;         // 0x38-byte blocks; EXIGetID does not read the probe times
    bool separate_probe = true;   // EXIProbe is its own function around the internal probe
    bool probe_ex = true;         // links an EXIProbeEx after EXIProbe
    bool select_variant = false;  // a second selected-bit setter, as Pokemon Channel's
    std::uint32_t table = kBlocks;
};

SdkShape modern_shape() { return {}; }

SdkShape old_shape() {
    SdkShape shape;
    shape.old_sdk = true;
    shape.separate_probe = false;
    shape.probe_ex = false;
    return shape;
}

struct Sdk {
    Text own;
    Text& text;
    std::uint32_t probe = 0;
    std::uint32_t extra_select = 0;
    std::uint32_t table = 0;
    std::array<std::uint32_t, riftwii::kGcExiHooks> expected{};

    explicit Sdk(const SdkShape& shape) : text(own) { build(shape); }
    // Another SDK in the same code, for a game linking two.
    Sdk(const SdkShape& shape, Text& shared) : text(shared) { build(shape); }

    void build(const SdkShape& shape) {
        const bool old_sdk = shape.old_sdk;
        table = shape.table;
        auto add = [&](const std::vector<std::uint32_t>& body, std::uint32_t size = 0x60) {
            return text.add_function(old_sdk, table, body, size);
        };
        expected[GC_EXI_IMM] = add({0x90040010, 0x90040014, 0x60000002});
        expected[GC_EXI_DMA] = add({0x60000003});
        expected[GC_EXI_SYNC] = add({});
        expected[GC_EXI_SELECT] = add({0x60000004, kNop});
        expected[GC_EXI_DESELECT] = add({clear_bit(2)});
        expected[GC_EXI_LOCK] = add({0x60000010});
        expected[GC_EXI_UNLOCK] = add({clear_bit(4)});
        probe = add({});
        if (shape.separate_probe) {
            expected[GC_EXI_PROBE] = add({kNop, kNop});
            if (shape.probe_ex) expected[GC_EXI_PROBE_EX] = add({kNop, kNop, kMinusOne});
        } else {
            expected[GC_EXI_PROBE] = probe;
        }
        expected[GC_EXI_ATTACH] = add({0x60000008, kNop});
        expected[GC_EXI_GET_ID] = add({old_sdk ? kNop : kProbeTimes, kNop, kNop, kNop, kNop});
        text.call(expected[GC_EXI_SELECT], 1, probe);
        text.call(expected[GC_EXI_ATTACH], 1, probe);
        text.call(expected[GC_EXI_GET_ID], 1, expected[GC_EXI_LOCK]);
        text.call(expected[GC_EXI_GET_ID], 2, expected[GC_EXI_SELECT]);
        text.call(expected[GC_EXI_GET_ID], 3, expected[GC_EXI_IMM]);
        text.call(expected[GC_EXI_GET_ID], 4, expected[GC_EXI_SYNC]);
        if (shape.separate_probe) {
            for (unsigned index : {GC_EXI_PROBE, GC_EXI_PROBE_EX}) {
                if (expected[index] == 0) continue;
                text.call(expected[index], 0, probe);
                text.call(expected[index], 1, expected[GC_EXI_GET_ID]);
            }
        }
        if (shape.select_variant) {
            // Like Pokemon Channel: another select sets bit 2, calls the
            // probe and GetID, but GetID itself calls only the bus selector.
            extra_select = add({0x60000004, kNop, kNop}, 0x140);
            text.call(extra_select, 1, probe);
            text.call(extra_select, 2, expected[GC_EXI_GET_ID]);
        }
    }

    riftwii::GcExiFunctions find(const std::string& game_id = {}) {
        return riftwii::find_gc_exi_functions(text.ranges(), game_id);
    }

    void expect_found(const std::string& game_id = {}) {
        const auto found = find(game_id);
        EXPECT_EQ(found.blocks, table);
        EXPECT_EQ(found.resolving_tables, 1u);
        for (unsigned index = 0; index < expected.size(); ++index)
            EXPECT_EQ(found.hooks[index], expected[index]);
        EXPECT_EQ(found.probe_ex_absent, expected[GC_EXI_PROBE_EX] == 0);
    }

    void expect_rejected(const std::string& game_id = {}) {
        const auto found = find(game_id);
        EXPECT_EQ(found.blocks, 0u);
        for (std::uint32_t address : found.hooks) EXPECT_EQ(address, 0u);
        EXPECT_TRUE(!found.probe_ex_absent);
    }
};

void test_supported_layouts() {
    Sdk modern(modern_shape());
    modern.expect_found();
    Sdk old(old_shape());
    old.expect_found();
    SdkShape with_variant = modern_shape();
    with_variant.select_variant = true;
    Sdk select_variant(with_variant);
    select_variant.expect_found();
}

// The 2001 SDK's 0x38-byte blocks, also with an EXIProbe and EXIProbeEx of
// their own (all eleven hooked), and without the EXIProbeEx (EXIProbe the
// internal probe, as Luigi's Mansion USA; ten hooked).
void test_old_sdk_with_and_without_probe_ex() {
    SdkShape with_probe_ex = old_shape();
    with_probe_ex.separate_probe = true;
    with_probe_ex.probe_ex = true;
    Sdk old_with(with_probe_ex);
    old_with.expect_found();
    EXPECT_TRUE(old_with.expected[GC_EXI_PROBE_EX] != 0);

    Sdk old_without(old_shape());
    const auto found = old_without.find();
    EXPECT_EQ(found.blocks, kBlocks);
    EXPECT_EQ(found.hooks[GC_EXI_PROBE], old_without.probe);
    EXPECT_EQ(found.hooks[GC_EXI_PROBE_EX], 0u);
    EXPECT_TRUE(found.probe_ex_absent);
}

void test_missing_or_ambiguous_functions() {
    Sdk missing_select(modern_shape());
    missing_select.text.put(missing_select.expected[GC_EXI_SELECT] + 16, kNop);
    missing_select.expect_rejected();

    Sdk missing_probe_ex(modern_shape());
    missing_probe_ex.text.put(missing_probe_ex.expected[GC_EXI_PROBE_EX] + 24, kNop);
    missing_probe_ex.expect_rejected();

    SdkShape with_variant = modern_shape();
    with_variant.select_variant = true;
    Sdk ambiguous_select(with_variant);
    // If GetID calls both candidates, there is still no safe choice.
    ambiguous_select.text.call(ambiguous_select.expected[GC_EXI_GET_ID], 5, ambiguous_select.extra_select);
    ambiguous_select.expect_rejected();

    Sdk ambiguous_get_id(modern_shape());
    const auto duplicate = ambiguous_get_id.text.add_function(false, kBlocks, {kProbeTimes, kNop, kNop});
    ambiguous_get_id.text.call(duplicate, 1, ambiguous_get_id.expected[GC_EXI_LOCK]);
    ambiguous_get_id.text.call(duplicate, 2, ambiguous_get_id.expected[GC_EXI_IMM]);
    ambiguous_get_id.expect_rejected();

    Sdk unrelated_select(with_variant);
    // A state-bit match outside GetID's call graph must not replace a
    // missing selector, even when the other candidate looks plausible.
    unrelated_select.text.put(unrelated_select.expected[GC_EXI_SELECT] + 16, kNop);
    unrelated_select.expect_rejected();
}

// Need for Speed: Underground 2 USA (#318): another table indexed the same
// way has far more users than the EXI blocks. The count must not pick it.
void test_decoy_table_with_more_users() {
    Sdk sdk(modern_shape());
    for (int i = 0; i < 40; ++i) sdk.text.add_function(false, kDecoyBlocks, {kNop});
    sdk.expect_found();
}

// Two tables that each resolve every function: no guess, the card stays off,
// and the result says how many resolved.
void test_two_resolving_tables_are_rejected() {
    Text shared;
    Sdk first(modern_shape(), shared);
    SdkShape second_shape = modern_shape();
    second_shape.table = kOtherBlocks;
    Sdk second(second_shape, shared);
    const auto found = first.find();
    EXPECT_EQ(found.blocks, 0u);
    EXPECT_EQ(found.resolving_tables, 2u);
    for (std::uint32_t address : found.hooks) EXPECT_EQ(address, 0u);

    // Across the two block sizes too.
    Text mixed;
    Sdk modern(modern_shape(), mixed);
    SdkShape old_other = old_shape();
    old_other.table = kOtherBlocks;
    Sdk old(old_other, mixed);
    const auto both = modern.find();
    EXPECT_EQ(both.blocks, 0u);
    EXPECT_EQ(both.resolving_tables, 2u);
}

// Resident Evil 2 USA (#318): a 0x40-byte SDK without EXIProbeEx, taken
// only for the listed ID and only when nothing could be a missed one.
SdkShape no_probe_ex_shape() {
    SdkShape shape = modern_shape();
    shape.probe_ex = false;
    return shape;
}

void test_listed_game_without_probe_ex() {
    Sdk sdk(no_probe_ex_shape());
    sdk.expect_found("GHAE08");
    EXPECT_TRUE(sdk.find("GHAE08").probe_ex_absent);
    sdk.expect_rejected();
    sdk.expect_rejected("GHAP08");  // another region: listed only once checked
    sdk.expect_rejected("GHAE0");
    sdk.expect_rejected("GMSE01");
    // A listed game whose code does have an EXIProbeEx still hooks it.
    Sdk with_probe_ex(modern_shape());
    with_probe_ex.expect_found("GHAE08");
}

void test_listed_game_negatives() {
    // Something like an EXIProbeEx (it calls the probe and EXIGetID) that
    // the finder did not take as one: too long for EXIProbe, no -1.
    Sdk probe_ex_like(no_probe_ex_shape());
    const std::uint32_t like = probe_ex_like.text.add_function(false, kBlocks, {kNop, kNop}, 0x140);
    probe_ex_like.text.call(like, 0, probe_ex_like.probe);
    probe_ex_like.text.call(like, 1, probe_ex_like.expected[GC_EXI_GET_ID]);
    probe_ex_like.expect_rejected("GHAE08");

    // EXIAttach does not directly follow EXIProbe: something sits where an
    // EXIProbeEx would. Built by hand so a function no table lists lands
    // between the two.
    {
        Text text;
        const bool old_sdk = false;
        std::array<std::uint32_t, riftwii::kGcExiHooks> e{};
        e[GC_EXI_IMM] = text.add_function(old_sdk, kBlocks, {0x90040010, 0x90040014, 0x60000002});
        e[GC_EXI_DMA] = text.add_function(old_sdk, kBlocks, {0x60000003});
        e[GC_EXI_SYNC] = text.add_function(old_sdk, kBlocks, {});
        e[GC_EXI_SELECT] = text.add_function(old_sdk, kBlocks, {0x60000004, kNop});
        e[GC_EXI_DESELECT] = text.add_function(old_sdk, kBlocks, {clear_bit(2)});
        e[GC_EXI_LOCK] = text.add_function(old_sdk, kBlocks, {0x60000010});
        e[GC_EXI_UNLOCK] = text.add_function(old_sdk, kBlocks, {clear_bit(4)});
        const std::uint32_t probe = text.add_function(old_sdk, kBlocks, {});
        e[GC_EXI_PROBE] = text.add_function(old_sdk, kBlocks, {kNop, kNop});
        text.add_function(old_sdk, kNoTable, {kNop});  // in EXIProbeEx's place
        e[GC_EXI_ATTACH] = text.add_function(old_sdk, kBlocks, {0x60000008, kNop});
        e[GC_EXI_GET_ID] = text.add_function(old_sdk, kBlocks, {kProbeTimes, kNop, kNop, kNop, kNop});
        text.call(e[GC_EXI_SELECT], 1, probe);
        text.call(e[GC_EXI_ATTACH], 1, probe);
        text.call(e[GC_EXI_GET_ID], 1, e[GC_EXI_LOCK]);
        text.call(e[GC_EXI_GET_ID], 2, e[GC_EXI_SELECT]);
        text.call(e[GC_EXI_GET_ID], 3, e[GC_EXI_IMM]);
        text.call(e[GC_EXI_GET_ID], 4, e[GC_EXI_SYNC]);
        text.call(e[GC_EXI_PROBE], 0, probe);
        text.call(e[GC_EXI_PROBE], 1, e[GC_EXI_GET_ID]);
        const auto found = riftwii::find_gc_exi_functions(text.ranges(), "GHAE08");
        EXPECT_EQ(found.blocks, 0u);
        EXPECT_EQ(found.hooks[GC_EXI_IMM], 0u);
        // The same code with that function taken out is accepted, so the
        // gap alone made the difference.
        Sdk control(no_probe_ex_shape());
        control.expect_found("GHAE08");
    }

    // A function outside the control blocks calls both the probe and
    // EXIGetID, as an EXIProbeEx the finder missed would.
    Sdk outside_caller(no_probe_ex_shape());
    const std::uint32_t outside = outside_caller.text.add_function(false, kNoTable, {kNop, kNop});
    outside_caller.text.call(outside, 0, outside_caller.probe);
    outside_caller.text.call(outside, 1, outside_caller.expected[GC_EXI_GET_ID]);
    outside_caller.expect_rejected("GHAE08");

    // A call to the probe from code whose function start cannot be found
    // (no blr within reach before it) is doubtful too.
    Sdk unknown_caller(no_probe_ex_shape());
    for (int i = 0; i < 0x110; ++i) unknown_caller.text.words.push_back(kNop);
    const std::uint32_t at = kBase + 4 * static_cast<std::uint32_t>(unknown_caller.text.words.size());
    unknown_caller.text.words.push_back(branch_link(at, unknown_caller.probe));
    unknown_caller.text.words.push_back(kBlr);
    unknown_caller.expect_rejected("GHAE08");
}

// A rejection hooks nothing but still reports what the finder did place, for
// the launch log.
void test_rejection_names_what_was_missing() {
    Sdk missing_select(modern_shape());
    missing_select.text.put(missing_select.expected[GC_EXI_SELECT] + 16, kNop);
    const auto found = missing_select.find();
    EXPECT_EQ(found.blocks, 0u);
    EXPECT_EQ(found.resolving_tables, 0u);
    EXPECT_EQ(found.partial[GC_EXI_SELECT], 0u);
    EXPECT_EQ(found.partial[GC_EXI_IMM], missing_select.expected[GC_EXI_IMM]);
    EXPECT_EQ(found.partial[GC_EXI_LOCK], missing_select.expected[GC_EXI_LOCK]);
    EXPECT_TRUE(std::string(riftwii::gc_exi_function_name(GC_EXI_GET_ID)) == "EXIGetID");
}

}  // namespace

int main() {
    test_supported_layouts();
    test_old_sdk_with_and_without_probe_ex();
    test_missing_or_ambiguous_functions();
    test_decoy_table_with_more_users();
    test_two_resolving_tables_are_rejected();
    test_listed_game_without_probe_ex();
    test_listed_game_negatives();
    test_rejection_names_what_was_missing();
    EXPECT_EQ(riftwii::find_gc_exi_functions({}).blocks, 0u);
    if (g_failures != 0) return 1;
    std::cout << "gcexi tests passed" << std::endl;
    return 0;
}
