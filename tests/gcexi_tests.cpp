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

// A tail call: b, no link.
std::uint32_t branch(std::uint32_t from, std::uint32_t to) {
    return 0x48000000u | ((to - from) & 0x03FFFFFC);
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

    // Two of EXIAttach's shape (EA's SDK in 007: NightFire): the one the
    // game calls is taken; with neither called, or both, no choice is safe.
    Sdk two_attaches(modern_shape());
    const auto unused_attach = two_attaches.text.add_function(false, kBlocks, {0x60000008, kNop});
    two_attaches.text.call(unused_attach, 1, two_attaches.probe);
    two_attaches.expect_rejected();
    const auto card_code = two_attaches.text.add_function(false, kNoTable, {kNop});
    two_attaches.text.call(card_code, 0, two_attaches.expected[GC_EXI_ATTACH]);
    two_attaches.expect_found();
    two_attaches.text.call(card_code, 1, unused_attach);
    two_attaches.expect_rejected();

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

using Reason = riftwii::GcExiFunctions::ListedRefusal;

// A listed game refused, by the check `reason`, at `at` (kAnyAddress: not
// checked), and the loader's gate saying so.
constexpr std::uint32_t kAnyAddress = 0xFFFFFFFF;
void expect_listed_refusal(const std::vector<riftwii::CodeRange>& ranges, Reason reason, std::uint32_t at) {
    const auto found = riftwii::find_gc_exi_functions(ranges, "GHAE08");
    EXPECT_EQ(found.blocks, 0u);
    for (std::uint32_t address : found.hooks) EXPECT_EQ(address, 0u);
    EXPECT_TRUE(found.listed_refusal == reason);
    if (at != kAnyAddress) EXPECT_EQ(found.listed_refusal_at, at);
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(found, ranges).verdict == riftwii::GcExiVerdict::ListedRefused);
}

// Each case is refused by the check named in its assertion. Where another
// check would refuse it too, the comment says so: the checks overlap on
// purpose, and the assertion names the first that refuses.
void test_listed_game_negatives() {
    // Something like an EXIProbeEx (it calls the probe and EXIGetID) that
    // the finder did not take as one: too long for EXIProbe, no -1. Refused
    // because EXIProbe is not then the only function calling both; its
    // branch to the probe (after the last placed function) would fail the
    // probe-caller check as well.
    Sdk probe_ex_like(no_probe_ex_shape());
    const std::uint32_t like = probe_ex_like.text.add_function(false, kBlocks, {kNop, kNop}, 0x140);
    probe_ex_like.text.call(like, 0, probe_ex_like.probe);
    probe_ex_like.text.call(like, 1, probe_ex_like.expected[GC_EXI_GET_ID]);
    expect_listed_refusal(probe_ex_like.text.ranges(), Reason::ProbeLike, kAnyAddress);

    // EXIAttach does not directly follow EXIProbe: something sits where an
    // EXIProbeEx would. Built by hand so a function no table lists lands
    // between the two; nothing else differs from an accepted SDK, so only
    // the attach-follows check refuses it.
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
        const std::uint32_t in_between = text.add_function(old_sdk, kNoTable, {kNop});  // in EXIProbeEx's place
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
        expect_listed_refusal(text.ranges(), Reason::AttachDoesNotFollow, in_between);
        // The same code with that function taken out is accepted, so the
        // gap alone made the difference.
        Sdk control(no_probe_ex_shape());
        control.expect_found("GHAE08");
    }

    // A function outside the control blocks calls both the probe and
    // EXIGetID, as an EXIProbeEx the finder missed would. No table lists it,
    // so only the probe-caller check sees it.
    Sdk outside_caller(no_probe_ex_shape());
    const std::uint32_t outside = outside_caller.text.add_function(false, kNoTable, {kNop, kNop});
    outside_caller.text.call(outside, 0, outside_caller.probe);
    outside_caller.text.call(outside, 1, outside_caller.expected[GC_EXI_GET_ID]);
    expect_listed_refusal(outside_caller.text.ranges(), Reason::ProbeCaller, outside + 16);

    // A call to the probe from code whose function start cannot be found
    // (no blr within reach before it): the probe-caller check refuses it.
    Sdk unknown_caller(no_probe_ex_shape());
    for (int i = 0; i < 0x110; ++i) unknown_caller.text.words.push_back(kNop);
    const std::uint32_t at = kBase + 4 * static_cast<std::uint32_t>(unknown_caller.text.words.size());
    unknown_caller.text.words.push_back(branch_link(at, unknown_caller.probe));
    unknown_caller.text.words.push_back(kBlr);
    expect_listed_refusal(unknown_caller.text.ranges(), Reason::ProbeCaller, at);
}

// A decoy that looks partly like the SDK: one function sets the selected and
// locked bits, another clears them, another stores a buffer and length, with
// enough users to be tried. It must not resolve on its own, and next to the
// real SDK the real table is taken.
void add_partial_decoy(Text& text) {
    text.add_function(false, kDecoyBlocks, {0x60000004, 0x60000010});
    text.add_function(false, kDecoyBlocks, {clear_bit(2), clear_bit(4)});
    text.add_function(false, kDecoyBlocks, {0x90040010, 0x90040014, 0x60000002});
    for (int i = 0; i < 10; ++i) text.add_function(false, kDecoyBlocks, {kNop});
}

void test_partial_decoy_does_not_resolve() {
    Text decoy_only;
    add_partial_decoy(decoy_only);
    const auto alone = riftwii::find_gc_exi_functions(decoy_only.ranges());
    EXPECT_EQ(alone.blocks, 0u);
    EXPECT_EQ(alone.resolving_tables, 0u);
    EXPECT_EQ(alone.tables_tried, 1u);
    EXPECT_EQ(alone.partial_blocks, kDecoyBlocks);
    EXPECT_TRUE(alone.partial[GC_EXI_IMM] != 0);  // the log names what the decoy placed

    Sdk sdk(modern_shape());
    add_partial_decoy(sdk.text);
    sdk.expect_found();
    EXPECT_EQ(sdk.find().tables_tried, 2u);
}

// #318 review: in the no-EXIProbeEx case every branch to the internal probe
// must come from a function already placed, or a clean span between them.
// These three pass the EXIProbe-alone and attach-follows checks (which see
// only bl calls up to a function's first blr), so only the probe-caller
// check refuses them; the pre-review check accepted all three.
void test_listed_game_strict_probe_callers() {
    // An EXIProbeEx with an early blr between its probe call and its
    // EXIGetID call: two functions to a nearest-blr search.
    Sdk early_blr(no_probe_ex_shape());
    const std::uint32_t split = early_blr.text.add_function(false, kBlocks, {kNop, kBlr, kNop, kMinusOne});
    early_blr.text.call(split, 0, early_blr.probe);
    early_blr.text.call(split, 2, early_blr.expected[GC_EXI_GET_ID]);
    expect_listed_refusal(early_blr.text.ranges(), Reason::ProbeCaller, split + 16);

    // A tail call (b) from outside: not a bl at all.
    Sdk tail_call(no_probe_ex_shape());
    const std::uint32_t outside = tail_call.text.add_function(false, kNoTable, {kNop, kNop});
    tail_call.text.put(outside + 16, branch(outside + 16, tail_call.probe));
    expect_listed_refusal(tail_call.text.ranges(), Reason::ProbeCaller, outside + 16);

    // A conditional tail call too (bc 12,2: beq).
    Sdk conditional(no_probe_ex_shape());
    const std::uint32_t cond = conditional.text.add_function(false, kNoTable, {kNop, kNop});
    const std::uint32_t at = cond + 16;
    conditional.text.put(at, 0x41820000u | ((conditional.probe - at) & 0xFFFC));
    expect_listed_refusal(conditional.text.ranges(), Reason::ProbeCaller, at);
}

// A 0x40-byte SDK without EXIProbeEx, with a function no table lists
// between EXIDeselect and EXILock (where Resident Evil 2 USA's EXI start-up
// sits), `gap_words` long; the tests fill it in.
struct GapSdk {
    Text text;
    std::uint32_t probe = 0;
    std::uint32_t gap = 0;
    std::uint32_t get_id = 0;

    explicit GapSdk(unsigned gap_words) {
        auto add = [&](const std::vector<std::uint32_t>& body) { return text.add_function(false, kBlocks, body); };
        const std::uint32_t imm = add({0x90040010, 0x90040014, 0x60000002});
        add({0x60000003});
        const std::uint32_t sync = add({});
        const std::uint32_t select = add({0x60000004, kNop});
        add({clear_bit(2)});
        gap = text.add_function(false, kNoTable, std::vector<std::uint32_t>(gap_words, kNop), 16 + 4 * gap_words + 8);
        const std::uint32_t lock = add({0x60000010});
        add({clear_bit(4)});
        probe = add({});
        const std::uint32_t exi_probe = add({kNop, kNop});
        const std::uint32_t attach = add({0x60000008, kNop});
        get_id = add({kProbeTimes, kNop, kNop, kNop, kNop});
        text.call(select, 1, probe);
        text.call(attach, 1, probe);
        text.call(get_id, 1, lock);
        text.call(get_id, 2, select);
        text.call(get_id, 3, imm);
        text.call(get_id, 4, sync);
        text.call(exi_probe, 0, probe);
        text.call(exi_probe, 1, get_id);
    }

    bool found() { return riftwii::find_gc_exi_functions(text.ranges(), "GHAE08").blocks == kBlocks; }
};

// Rule (b): a branch to the probe from code between placed functions passes
// only when that span holds no EXIGetID call and no li r3,-1.
void test_listed_game_bracketed_callers() {
    GapSdk plain(4);
    EXPECT_TRUE(plain.found());

    GapSdk init_like(4);  // probes channels 0 and 1, as an EXI start-up does
    init_like.text.call(init_like.gap, 0, init_like.probe);
    init_like.text.call(init_like.gap, 2, init_like.probe);
    EXPECT_TRUE(init_like.found());

    GapSdk split(4);  // an EXIProbeEx with an early return between its two calls
    split.text.call(split.gap, 0, split.probe);
    split.text.put(split.gap + 16 + 4, kBlr);
    split.text.call(split.gap, 2, split.get_id);
    expect_listed_refusal(split.text.ranges(), Reason::ProbeCaller, split.gap + 16);

    GapSdk minus_one(4);  // a probe call and a -1: an EXIProbeEx with GetID inlined away
    minus_one.text.call(minus_one.gap, 0, minus_one.probe);
    minus_one.text.put(minus_one.gap + 16 + 8, kMinusOne);
    expect_listed_refusal(minus_one.text.ranges(), Reason::ProbeCaller, minus_one.gap + 16);

    // The same code with an EXIProbeEx found is never held to the rule:
    // a game not on the list rejects the gap SDK (no EXIProbeEx), as before.
    GapSdk unlisted(4);
    EXPECT_EQ(riftwii::find_gc_exi_functions(unlisted.text.ranges(), "GMSE01").blocks, 0u);
}

// One table resolving in each layout: rejected, and the log's partial list
// is the first resolving table's, with its address.
void test_cross_layout_partial() {
    Text mixed;
    Sdk modern(modern_shape(), mixed);
    SdkShape old_other = old_shape();
    old_other.table = kOtherBlocks;
    Sdk old(old_other, mixed);
    add_partial_decoy(mixed);  // a decoy placing more than nothing must not take the log
    const auto found = modern.find();
    EXPECT_EQ(found.blocks, 0u);
    EXPECT_EQ(found.resolving_tables, 2u);
    EXPECT_EQ(found.partial_blocks, kBlocks);
    for (unsigned index = 0; index < riftwii::kGcExiHooks; ++index)
        EXPECT_EQ(found.partial[index], modern.expected[index]);
}

// The loader's all-or-nothing gate (decide_gc_exi_hooks), case by case.
void test_hook_decision() {
    using riftwii::GcExiVerdict;
    Sdk sdk(modern_shape());
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(sdk.find(), sdk.text.ranges()).verdict == GcExiVerdict::Hook);

    Sdk listed(no_probe_ex_shape());
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(listed.find("GHAE08"), listed.text.ranges()).verdict ==
                GcExiVerdict::Hook);

    // A function starting with a branch: everything found, nothing hooked.
    Sdk branching(modern_shape());
    branching.text.put(branching.expected[GC_EXI_IMM], 0x48000008);  // b +8
    const auto with_branch = branching.find();
    EXPECT_EQ(with_branch.blocks, kBlocks);
    const auto decided = riftwii::decide_gc_exi_hooks(with_branch, branching.text.ranges());
    EXPECT_TRUE(decided.verdict == GcExiVerdict::StartsWithBranch);
    EXPECT_TRUE(decided.branching[GC_EXI_IMM]);
    EXPECT_TRUE(!decided.branching[GC_EXI_DMA]);

    // Two roles on one address, or two hooks closer than kGcExiHookSpan (the
    // branch word and the word its replay jumps back to): never hooked.
    auto same = sdk.find();
    same.hooks[GC_EXI_SYNC] = same.hooks[GC_EXI_IMM];
    const auto same_decided = riftwii::decide_gc_exi_hooks(same, sdk.text.ranges());
    EXPECT_TRUE(same_decided.verdict == GcExiVerdict::Overlapping);
    EXPECT_EQ(same_decided.overlap_first, static_cast<unsigned>(GC_EXI_IMM));
    EXPECT_EQ(same_decided.overlap_second, static_cast<unsigned>(GC_EXI_SYNC));
    auto adjacent = sdk.find();
    adjacent.hooks[GC_EXI_SYNC] = adjacent.hooks[GC_EXI_IMM] + 4;
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(adjacent, sdk.text.ranges()).verdict == GcExiVerdict::Overlapping);
    auto below = sdk.find();
    below.hooks[GC_EXI_IMM] = below.hooks[GC_EXI_SYNC] + 4;  // order does not matter
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(below, sdk.text.ranges()).verdict == GcExiVerdict::Overlapping);
    auto spaced = sdk.find();
    spaced.hooks[GC_EXI_SYNC] = spaced.hooks[GC_EXI_IMM] + riftwii::kGcExiHookSpan;
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(spaced, sdk.text.ranges()).verdict == GcExiVerdict::Hook);

    // A result with one hook missing is never hooked, even with blocks set.
    auto incomplete = sdk.find();
    incomplete.hooks[GC_EXI_SYNC] = 0;
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(incomplete, sdk.text.ranges()).verdict == GcExiVerdict::Incomplete);
    // And EXIProbeEx 0 without the finder saying it is absent.
    auto no_probe_ex = sdk.find();
    no_probe_ex.hooks[GC_EXI_PROBE_EX] = 0;
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(no_probe_ex, sdk.text.ranges()).verdict == GcExiVerdict::Incomplete);

    Text shared;
    Sdk first(modern_shape(), shared);
    SdkShape second_shape = modern_shape();
    second_shape.table = kOtherBlocks;
    Sdk second(second_shape, shared);
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(first.find(), shared.ranges()).verdict ==
                GcExiVerdict::SeveralTables);

    Sdk missing(modern_shape());
    missing.text.put(missing.expected[GC_EXI_SELECT] + 16, kNop);
    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(missing.find(), missing.text.ranges()).verdict ==
                GcExiVerdict::NotAllFound);

    EXPECT_TRUE(riftwii::decide_gc_exi_hooks(riftwii::find_gc_exi_functions({}), {}).verdict ==
                GcExiVerdict::NoTables);
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
    test_partial_decoy_does_not_resolve();
    test_listed_game_strict_probe_callers();
    test_listed_game_bracketed_callers();
    test_cross_layout_partial();
    test_hook_decision();
    test_rejection_names_what_was_missing();
    EXPECT_EQ(riftwii::find_gc_exi_functions({}).blocks, 0u);
    if (g_failures != 0) return 1;
    std::cout << "gcexi tests passed" << std::endl;
    return 0;
}
