// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Invented instruction sequences exercising the EXI finder's state and
// call relationships. No game executable bytes are stored in these tests.
#include "riftwii/gcexi.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

#include "gc_card.h"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

constexpr std::uint32_t kBase = 0x80004000;
constexpr std::uint32_t kBlocks = 0x80301000;
constexpr std::uint32_t kBlr = 0x4E800020;
constexpr std::uint32_t kNop = 0x60000000;

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

    std::uint32_t add_function(bool old_sdk, const std::vector<std::uint32_t>& body,
                               std::uint32_t size = 0x60) {
        const auto start = kBase + 4 * static_cast<std::uint32_t>(words.size());
        words.push_back(0x7C0802A6);  // mflr r0
        words.push_back(old_sdk ? 0x1C030038 : 0x54033032);  // channel times the block size
        words.push_back(0x3C808030);  // lis r4,0x8030
        words.push_back(0x38841000);  // addi r4,r4,0x1000
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

struct Sdk {
    Text text;
    std::uint32_t probe = 0;
    std::uint32_t extra_select = 0;
    std::array<std::uint32_t, riftwii::kGcExiHooks> expected{};

    explicit Sdk(bool old_sdk, bool select_variant = false) {
        expected[GC_EXI_IMM] = text.add_function(old_sdk, {0x90040010, 0x90040014, 0x60000002});
        expected[GC_EXI_DMA] = text.add_function(old_sdk, {0x60000003});
        expected[GC_EXI_SYNC] = text.add_function(old_sdk, {});
        expected[GC_EXI_SELECT] = text.add_function(old_sdk, {0x60000004, kNop});
        expected[GC_EXI_DESELECT] = text.add_function(old_sdk, {clear_bit(2)});
        expected[GC_EXI_LOCK] = text.add_function(old_sdk, {0x60000010});
        expected[GC_EXI_UNLOCK] = text.add_function(old_sdk, {clear_bit(4)});
        probe = text.add_function(old_sdk, {});
        if (old_sdk) {
            expected[GC_EXI_PROBE] = probe;
        } else {
            expected[GC_EXI_PROBE] = text.add_function(old_sdk, {kNop, kNop});
            expected[GC_EXI_PROBE_EX] = text.add_function(old_sdk, {kNop, kNop, 0x3860FFFF});
        }
        expected[GC_EXI_ATTACH] = text.add_function(old_sdk, {0x60000008, kNop});
        expected[GC_EXI_GET_ID] = text.add_function(old_sdk, {
            old_sdk ? kNop : 0x386330C0, kNop, kNop, kNop, kNop
        });
        text.call(expected[GC_EXI_SELECT], 1, probe);
        text.call(expected[GC_EXI_ATTACH], 1, probe);
        text.call(expected[GC_EXI_GET_ID], 1, expected[GC_EXI_LOCK]);
        text.call(expected[GC_EXI_GET_ID], 2, expected[GC_EXI_SELECT]);
        text.call(expected[GC_EXI_GET_ID], 3, expected[GC_EXI_IMM]);
        text.call(expected[GC_EXI_GET_ID], 4, expected[GC_EXI_SYNC]);
        if (!old_sdk) {
            for (unsigned index : {GC_EXI_PROBE, GC_EXI_PROBE_EX}) {
                text.call(expected[index], 0, probe);
                text.call(expected[index], 1, expected[GC_EXI_GET_ID]);
            }
        }
        if (select_variant) {
            // Like Pokemon Channel: another select sets bit 2, calls the
            // probe and GetID, but GetID itself calls only the bus selector.
            extra_select = text.add_function(old_sdk, {0x60000004, kNop, kNop}, 0x140);
            text.call(extra_select, 1, probe);
            text.call(extra_select, 2, expected[GC_EXI_GET_ID]);
        }
    }

    void expect_found() {
        const auto found = riftwii::find_gc_exi_functions(text.ranges());
        EXPECT_EQ(found.blocks, kBlocks);
        for (unsigned index = 0; index < expected.size(); ++index)
            EXPECT_EQ(found.hooks[index], expected[index]);
    }

    void expect_rejected() {
        const auto found = riftwii::find_gc_exi_functions(text.ranges());
        EXPECT_EQ(found.blocks, 0u);
        for (std::uint32_t address : found.hooks) EXPECT_EQ(address, 0u);
    }
};

void test_supported_layouts() {
    Sdk modern(false);
    modern.expect_found();
    Sdk old(true);
    old.expect_found();
    Sdk select_variant(false, true);
    select_variant.expect_found();
}

void test_missing_or_ambiguous_functions() {
    Sdk missing_select(false);
    missing_select.text.put(missing_select.expected[GC_EXI_SELECT] + 16, kNop);
    missing_select.expect_rejected();

    Sdk missing_probe_ex(false);
    missing_probe_ex.text.put(missing_probe_ex.expected[GC_EXI_PROBE_EX] + 24, kNop);
    missing_probe_ex.expect_rejected();

    Sdk ambiguous_select(false, true);
    // If GetID calls both candidates, there is still no safe choice.
    ambiguous_select.text.call(ambiguous_select.expected[GC_EXI_GET_ID], 5,
                               ambiguous_select.extra_select);
    ambiguous_select.expect_rejected();

    Sdk ambiguous_get_id(false);
    const auto duplicate = ambiguous_get_id.text.add_function(false, {0x386330C0, kNop, kNop});
    ambiguous_get_id.text.call(duplicate, 1, ambiguous_get_id.expected[GC_EXI_LOCK]);
    ambiguous_get_id.text.call(duplicate, 2, ambiguous_get_id.expected[GC_EXI_IMM]);
    ambiguous_get_id.expect_rejected();

    Sdk unrelated_select(false, true);
    // A state-bit match outside GetID's call graph must not replace a
    // missing selector, even when the other candidate looks plausible.
    unrelated_select.text.put(unrelated_select.expected[GC_EXI_SELECT] + 16, kNop);
    unrelated_select.expect_rejected();
}

}  // namespace

int main() {
    test_supported_layouts();
    test_missing_or_ambiguous_functions();
    EXPECT_EQ(riftwii::find_gc_exi_functions({}).blocks, 0u);
    if (g_failures != 0) return 1;
    std::cout << "gcexi tests passed" << std::endl;
    return 0;
}
