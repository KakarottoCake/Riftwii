// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A GameCube pack's <memory> patches worked out against the game as the
// loader stages it (riftwii/gcmempatch.hpp): writes kept in order, each
// patch seeing the ones before it, memory outside the sections read as
// zeros, and nothing written outside the writable regions.
#include "riftwii/gcmempatch.hpp"

#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using Bytes = std::vector<std::uint8_t>;

namespace {

constexpr std::uint32_t kText = 0x80003100;

// A small text section: a few instructions, the last a blr.
Bytes sample_text() {
    return Bytes{0x7C, 0x08, 0x02, 0xA6,   // mflr r0         at 0x80003100
                 0x38, 0x60, 0x00, 0x00,   // li r3,0         at 0x80003104
                 0x60, 0x00, 0x00, 0x00,   // nop             at 0x80003108
                 0x4E, 0x80, 0x00, 0x20};  // blr             at 0x8000310C
}

// All of MEM1 below the loader's own area at its top.
const std::vector<MemoryRegion> kWritable{{0x80000000, 0x017D0000}};

MemoryPatch plain(std::uint32_t at, Bytes value, Bytes original = {}) {
    MemoryPatch p;
    p.has_offset = true;
    p.offset = at;
    p.value = std::move(value);
    p.original = std::move(original);
    return p;
}

void test_plain_and_original() {
    const Bytes text = sample_text();
    const std::vector<GcStagedSection> sections{{kText, text.data(), text.size()}};
    // The original matches the staged code: written. One that doesn't: skipped, with a note.
    std::vector<MemoryPatch> patches{plain(0x80003104, {0x38, 0x60, 0x00, 0x01}, {0x38, 0x60, 0x00, 0x00}),
                                     plain(0x80003108, {0x48, 0x00, 0x00, 0x10}, {0x12, 0x34, 0x56, 0x78})};
    std::vector<GcMemoryWrite> writes;
    std::vector<std::string> notes;
    std::string error;
    EXPECT_TRUE(plan_gc_memory_writes(patches, sections, kWritable, writes, notes, error));
    EXPECT_EQ(writes.size(), 1u);
    if (!writes.empty()) {
        EXPECT_EQ(writes[0].address, 0x80003104u);
        EXPECT_TRUE(writes[0].bytes == (Bytes{0x38, 0x60, 0x00, 0x01}));
    }
    EXPECT_EQ(notes.size(), 2u);
    if (notes.size() == 2) EXPECT_TRUE(notes[1].find("skipped") != std::string::npos);
    // The staged code itself is not changed: the loader copies the writes in last.
    EXPECT_TRUE(text == sample_text());
}

void test_later_patch_sees_earlier() {
    const Bytes text = sample_text();
    const std::vector<GcStagedSection> sections{{kText, text.data(), text.size()}};
    // A code cave in low memory (outside every section, so zeros), then a
    // patch whose original is what the first one wrote.
    std::vector<MemoryPatch> patches{plain(0x80001820, {0x94, 0x21, 0xFF, 0xF0}),
                                     plain(0x80001820, {0x60, 0x00, 0x00, 0x00}, {0x94, 0x21, 0xFF, 0xF0}),
                                     plain(0x80001900, {0xAA}, {0x00})};
    std::vector<GcMemoryWrite> writes;
    std::vector<std::string> notes;
    std::string error;
    EXPECT_TRUE(plan_gc_memory_writes(patches, sections, kWritable, writes, notes, error));
    EXPECT_EQ(writes.size(), 3u);
    for (const std::string& n : notes) EXPECT_TRUE(n.find("skipped") == std::string::npos);
}

void test_search_and_ocarina() {
    const Bytes text = sample_text();
    const std::vector<GcStagedSection> sections{{kText, text.data(), text.size()}};
    MemoryPatch search;
    search.search = true;
    search.align = 4;
    search.original = {0x60, 0x00, 0x00, 0x00};
    search.value = {0x38, 0x80, 0x00, 0x07};
    MemoryPatch ocarina;
    ocarina.ocarina = true;
    ocarina.has_offset = true;
    ocarina.offset = 0x80001800;
    ocarina.value = {0x7C, 0x08, 0x02, 0xA6};
    std::vector<GcMemoryWrite> writes;
    std::vector<std::string> notes;
    std::string error;
    EXPECT_TRUE(plan_gc_memory_writes({search, ocarina}, sections, kWritable, writes, notes, error));
    EXPECT_EQ(writes.size(), 2u);
    if (writes.size() == 2) {
        // The nop at 0x80003108 found and replaced.
        EXPECT_EQ(writes[0].address, 0x80003108u);
        // The blr after the found code becomes a branch to 0x80001800.
        EXPECT_EQ(writes[1].address, 0x8000310Cu);
        const std::uint32_t branch = 0x48000000u | ((0x80001800u - 0x8000310Cu) & 0x03FFFFFCu);
        const Bytes expected{static_cast<std::uint8_t>(branch >> 24), static_cast<std::uint8_t>(branch >> 16),
                             static_cast<std::uint8_t>(branch >> 8), static_cast<std::uint8_t>(branch)};
        EXPECT_TRUE(writes[1].bytes == expected);
    }
}

void test_outside_writable() {
    const Bytes text = sample_text();
    const std::vector<GcStagedSection> sections{{kText, text.data(), text.size()}};
    // Into the loader's own area at MEM1's top: refused, the launch stops.
    std::vector<GcMemoryWrite> writes;
    std::vector<std::string> notes;
    std::string error;
    EXPECT_FALSE(plan_gc_memory_writes({plain(0x817F0000, {1, 2, 3, 4})}, sections, kWritable, writes, notes, error));
    EXPECT_FALSE(error.empty());
}

}  // namespace

int main() {
    test_plain_and_original();
    test_later_patch_sees_earlier();
    test_search_and_ocarina();
    test_outside_writable();
    if (g_failures == 0) std::cout << "gcmempatch: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
