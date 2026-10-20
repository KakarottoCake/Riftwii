// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic PowerPC functions and the blob's shared callback rule: no game
// bytes or addresses. Buffer and callback lifetimes intentionally differ.
#include "riftwii/gcload.hpp"
#include "gc_load.h"

#include <iostream>
#include <vector>

namespace {

unsigned failures = 0;

void expect(bool condition, const char* description) {
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
        ++failures;
    }
}

constexpr std::uint32_t kBase = 0x80300000;
constexpr std::uint32_t kGroup = kBase + 0x20;
constexpr std::uint32_t kChangeGroup = kBase + 0x100;
constexpr std::uint32_t kSystemSetter = kBase + 0x120;
constexpr std::uint32_t kCurrentSetter = kBase + 0x140;
constexpr std::uint32_t kConstructor = kBase + 0x180;
constexpr std::uint32_t kNop = 0x60000000;
constexpr std::uint32_t kBlr = 0x4E800020;

std::uint32_t small_load(unsigned reg, std::int32_t offset) {
    return 0x800D0000u | (reg << 21) | static_cast<std::uint16_t>(offset);
}

std::uint32_t call(std::uint32_t address, std::uint32_t target) {
    return 0x48000001u | ((target - address) & 0x03FFFFFC);
}

struct Program {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x240);
    std::int32_t current;
    std::array<std::int32_t, 4> roots;

    void put(std::uint32_t address, std::uint32_t word) {
        const std::size_t offset = address - kBase;
        bytes[offset] = static_cast<std::uint8_t>(word >> 24);
        bytes[offset + 1] = static_cast<std::uint8_t>(word >> 16);
        bytes[offset + 2] = static_cast<std::uint8_t>(word >> 8);
        bytes[offset + 3] = static_cast<std::uint8_t>(word);
    }

    void add_setter(std::uint32_t address, std::int32_t offset) {
        put(address, small_load(0, offset));
        put(address + 4, 0x906D0000u | static_cast<std::uint16_t>(offset));
        put(address + 8, 0x7C030378);  // mr r3,r0
        put(address + 12, kBlr);
    }

    void add_comparison(std::uint32_t address, std::int32_t heap, std::int32_t root,
                        std::uint32_t setter) {
        put(address, small_load(3, heap));
        put(address + 4, small_load(0, root));
        put(address + 8, 0x7C030040);   // cmplw r3,r0
        put(address + 12, 0x4082000C);  // bne past replacement
        put(address + 16, 0x80610008);  // lwz r3,8(r1): new heap
        put(address + 20, call(address + 20, setter));
    }

    Program(std::int32_t displacement = 0x680, std::int32_t main_base = 0x40)
        : current(displacement), roots{main_base, main_base + 4, displacement - 4, displacement + 4} {
        put(kGroup, 0x7C0802A6);  // mflr r0
        put(kGroup + 4, small_load(3, roots[0]));
        put(kGroup + 8, kNop);  // a saved-group byte store may be between load and call
        put(kGroup + 12, call(kGroup + 12, kChangeGroup));
        put(kGroup + 16, small_load(3, roots[1]));
        put(kGroup + 20, 0x7FE4FB78);  // mr r4,r31
        put(kGroup + 24, call(kGroup + 24, kChangeGroup));
        put(kGroup + 28, kBlr);
        put(kChangeGroup, kBlr);
        add_setter(kSystemSetter, roots[2]);
        add_setter(kCurrentSetter, current);
        add_comparison(kConstructor, roots[2], roots[3], kSystemSetter);
        add_comparison(kConstructor + 24, current, roots[3], kCurrentSetter);
    }

    std::vector<riftwii::CodeRange> ranges() const { return {{kBase, bytes.data(), bytes.size()}}; }

    riftwii::GcLoadHeaps find() const {
        return riftwii::find_gc_load_heaps(ranges(), kGroup, current);
    }
};

void test_finder() {
    for (const auto offset : {0x680, -0x680}) {
        Program program(offset, offset < 0 ? -0x100 : 0x40);
        const auto found = program.find();
        expect(found.found, "find all four heap globals with either sign of r13 displacement");
        expect(found.offsets == program.roots, "return the discovered globals without fixed addresses");

        // Real DOLs have multiple text ranges, and a function can reach a
        // setter elsewhere. No contiguous host-memory assumption is needed.
        const std::vector<riftwii::CodeRange> split = {
            {kBase, program.bytes.data(), 0x100},
            {kBase + 0x100, program.bytes.data() + 0x100, program.bytes.size() - 0x100}};
        expect(riftwii::find_gc_load_heaps(split, kGroup, offset).found, "find globals across text ranges");
    }
    {
        Program program;
        program.put(kGroup + 24, call(kGroup + 24, kSystemSetter));
        expect(!program.find().found, "reject main heaps passed to different group functions");
    }
    {
        Program program;
        program.put(kGroup + 16, small_load(3, program.roots[0]));
        expect(!program.find().found, "reject duplicate main heap globals");
    }
    {
        Program program;
        program.put(kConstructor + 28, small_load(0, 0x700));
        expect(!program.find().found, "both comparisons must identify the same root");
    }
    {
        Program program;
        program.put(kCurrentSetter + 4, 0x906D0700);
        expect(!program.find().found, "the setter must write the heap it reads");
    }
    {
        Program program;
        expect(!riftwii::find_gc_load_heaps(program.ranges(), kGroup, program.current + 4).found,
               "constructor must agree with the loader's current heap");
        program.bytes.resize(kConstructor - kBase + 44);
        expect(!program.find().found, "reject a truncated constructor");
    }
    {
        Program program;
        program.add_comparison(kConstructor + 0x40, program.roots[2], program.roots[3], kSystemSetter);
        program.add_comparison(kConstructor + 0x58, program.current, program.roots[3], kCurrentSetter);
        expect(!program.find().found, "reject ambiguous constructors");
    }
    {
        Program program;
        program.put(kConstructor + 12, 0x40820008);
        expect(!program.find().found, "the unequal branch must skip the setter call");
    }
    {
        Program program;
        const std::vector<riftwii::CodeRange> missing_group = {
            {kBase + 0x100, program.bytes.data() + 0x100, program.bytes.size() - 0x100}};
        expect(!riftwii::find_gc_load_heaps(missing_group, kGroup, program.current).found,
               "reject missing group code");
        expect(!riftwii::find_gc_load_heaps({}, kGroup, program.current).found, "reject empty text");
    }
}

void test_callback_lifetimes() {
    const std::uint32_t roots[GC_LOAD_ROOT_HEAPS] = {0x81000000, 0x81100000, 0x81200000, 0x81300000};
    const std::uint32_t persistent = roots[0];
    const std::uint32_t scene = roots[1];
    const std::uint32_t temporary = 0x81400000;

    expect(gc_load_callback_heap(persistent, scene, roots) == 0,
           "game_usa: preserve persistent callback heap although buffer was queued on scene");
    for (std::uint32_t root : roots) {
        expect(gc_load_callback_heap(root, temporary, roots) == 0,
               "a main current heap wins even when the queued heap is temporary");
    }
    expect(gc_load_callback_heap(temporary, scene, roots) == scene,
           "room load: escape the full solid heap for callback allocations");
    expect(gc_load_callback_heap(temporary, persistent, roots) == persistent,
           "temporary heap protection also covers persistent queued loads");
    expect(gc_load_callback_heap(temporary, temporary, roots) == 0, "avoid an unnecessary heap change");
    expect(gc_load_callback_heap(temporary, 0, roots) == 0, "an untracked request keeps the game's heap");
    expect(gc_load_callback_heap(0, scene, roots) == 0, "do not invent a heap when none is current");

    // The runtime rereads the globals for every callback, including after
    // a restart. Classification follows their values, not cached pointers.
    std::uint32_t changed_roots[GC_LOAD_ROOT_HEAPS] = {temporary, scene, roots[2], roots[3]};
    expect(gc_load_callback_heap(temporary, scene, changed_roots) == 0,
           "recognize a main heap after its global value changes");
}

}  // namespace

int main() {
    test_finder();
    test_callback_lifetimes();
    if (failures == 0) std::cout << "gcload tests passed\n";
    return failures == 0 ? 0 : 1;
}
