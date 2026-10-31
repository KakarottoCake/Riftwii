// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Programs a GameCube game loads itself (riftwii/gcprograms.hpp and the
// blob's gc_programs.h): the ELF programs on the disc, where their words
// come from on the disc, the patches the blob puts into reads, the
// program's start taken by the blob, and RiftWii's words a program switch
// sets. The ELF headers are made up field by field, laid out as 007:
// NightFire's Nightfire.elf is (entry 0x80007100, segments from there).
#include "riftwii/gcprograms.hpp"

#include "gc_programs.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

using riftwii::GcDiscPatch;
using riftwii::GcElfProgram;
using riftwii::GcResidentRegion;

void put16(std::vector<std::uint8_t>& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

std::uint32_t get32(const std::vector<std::uint8_t>& b, std::size_t at) {
    return std::uint32_t(b[at]) << 24 | std::uint32_t(b[at + 1]) << 16 | std::uint32_t(b[at + 2]) << 8 | b[at + 3];
}

struct SegmentSpec {
    std::uint32_t type;
    std::uint32_t file_offset;
    std::uint32_t address;
    std::uint32_t file_bytes;
    std::uint32_t memory_bytes;
    std::uint32_t flags;
};

constexpr std::uint32_t kFloor = 0x80003100;
constexpr std::uint32_t kCeiling = 0x817D0000;
constexpr std::uint32_t kFileBytes = 0x211CDC;  // Nightfire.elf's size

// An ELF header and its program headers right after it, as EA's linker
// left them: the first 4 KiB of the file.
std::vector<std::uint8_t> make_elf(std::uint32_t entry, const std::vector<SegmentSpec>& segments) {
    std::vector<std::uint8_t> head(riftwii::kGcElfHeadBytes, 0);
    head[0] = 0x7F;
    head[1] = 'E';
    head[2] = 'L';
    head[3] = 'F';
    head[4] = 1;  // 32-bit
    head[5] = 2;  // big-endian
    head[6] = 1;
    put16(head, 0x10, 2);   // ET_EXEC
    put16(head, 0x12, 20);  // EM_PPC
    put32(head, 0x14, 1);
    put32(head, 0x18, entry);
    put32(head, 0x1C, 0x34);
    put16(head, 0x28, 0x34);
    put16(head, 0x2A, 0x20);
    put16(head, 0x2C, static_cast<std::uint16_t>(segments.size()));
    for (std::size_t i = 0; i < segments.size(); ++i) {
        const std::size_t at = 0x34 + 0x20 * i;
        put32(head, at, segments[i].type);
        put32(head, at + 0x04, segments[i].file_offset);
        put32(head, at + 0x08, segments[i].address);
        put32(head, at + 0x0C, segments[i].address);
        put32(head, at + 0x10, segments[i].file_bytes);
        put32(head, at + 0x14, segments[i].memory_bytes);
        put32(head, at + 0x18, segments[i].flags);
        put32(head, at + 0x1C, 1);
    }
    return head;
}

// Nightfire.elf's segments, the empty ones too (a .sbss2 of no bytes).
std::vector<SegmentSpec> nightfire_segments() {
    return {{1, 0x180, 0x80007100, 0x1C4, 0x1C4, 7},        {1, 0x360, 0x800072E0, 0x198F2C, 0x198F2C, 5},
            {1, 0x1992A0, 0x801A0220, 0x1C, 0x1C, 6},        {1, 0x1992C0, 0x801A0240, 0x15B44, 0x15B44, 4},
            {1, 0x1AEE20, 0x801B5DA0, 0x606BA, 0x606BA, 6}, {1, 0x20F4E0, 0x80216460, 0, 0xF7B90, 6},
            {1, 0x20F4E0, 0x8030E000, 0x13EA, 0x13EA, 6},    {1, 0x2108E0, 0x8030F400, 0, 0xB50, 6},
            {1, 0x2108E0, 0x8030FF60, 0x1188, 0x1188, 6},    {1, 0x211A80, 0x803110E8, 0, 0, 6}};
}

void test_parse() {
    const std::vector<std::uint8_t> head = make_elf(0x80007100, nightfire_segments());
    GcElfProgram program;
    std::string why;
    EXPECT_TRUE(riftwii::parse_gc_elf(head.data(), head.size(), kFileBytes, kFloor, kCeiling, program, why));
    EXPECT_EQ(program.entry, 0x80007100u);
    // The segment with no memory is left out.
    EXPECT_EQ(program.segments.size(), static_cast<std::size_t>(9));
    if (program.segments.size() == 9) {
        EXPECT_TRUE(program.segments[0].executable);
        EXPECT_TRUE(program.segments[1].executable);
        EXPECT_TRUE(!program.segments[3].executable);
        EXPECT_EQ(program.segments[1].file_offset, 0x360u);
        EXPECT_EQ(program.segments[5].file_bytes, 0u);
        EXPECT_EQ(program.segments[5].memory_bytes, 0xF7B90u);
    }

    auto refused = [](std::vector<std::uint8_t> bad, std::uint32_t file_bytes = kFileBytes) {
        GcElfProgram out;
        std::string reason;
        const bool parsed = riftwii::parse_gc_elf(bad.data(), bad.size(), file_bytes, kFloor, kCeiling, out, reason);
        return !parsed && !reason.empty() && out.segments.empty();
    };
    std::vector<std::uint8_t> bad = head;
    bad[1] = 'X';
    EXPECT_TRUE(refused(bad));
    bad = head;
    bad[5] = 1;  // little-endian
    EXPECT_TRUE(refused(bad));
    bad = head;
    put16(bad, 0x10, 3);  // a shared object
    EXPECT_TRUE(refused(bad));
    bad = head;
    put16(bad, 0x12, 3);  // x86
    EXPECT_TRUE(refused(bad));
    // No machine named, as EA's linker left it in NightFire's loaders and
    // driving.elf: still taken.
    std::vector<std::uint8_t> unnamed = head;
    put16(unnamed, 0x12, 0);
    EXPECT_TRUE(riftwii::parse_gc_elf(unnamed.data(), unnamed.size(), kFileBytes, kFloor, kCeiling, program, why));
    bad = head;
    put16(bad, 0x2A, 0x38);
    EXPECT_TRUE(refused(bad));
    bad = head;
    put32(bad, 0x1C, riftwii::kGcElfHeadBytes - 0x20);  // the table runs past the bytes read
    EXPECT_TRUE(refused(bad));
    EXPECT_TRUE(refused(std::vector<std::uint8_t>(head.begin(), head.begin() + 0x30)));
    // A segment past the file's end.
    EXPECT_TRUE(refused(head, 0x211000));
    // Segments below the game's floor, past the ceiling, wrapping, unaligned.
    std::vector<SegmentSpec> segments = nightfire_segments();
    segments[2].address = 0x80003000;
    EXPECT_TRUE(refused(make_elf(0x80007100, segments)));
    segments = nightfire_segments();
    segments[8].address = kCeiling - 0x1000;
    EXPECT_TRUE(refused(make_elf(0x80007100, segments)));
    segments = nightfire_segments();
    segments[5].memory_bytes = 0xFFFFFFF0u;
    EXPECT_TRUE(refused(make_elf(0x80007100, segments)));
    segments = nightfire_segments();
    segments[6].address += 2;
    EXPECT_TRUE(refused(make_elf(0x80007100, segments)));
    // Two segments over the same memory.
    segments = nightfire_segments();
    segments[3].address = 0x801A0230;
    EXPECT_TRUE(refused(make_elf(0x80007100, segments)));
    // The entry in data, in a zeroed tail, unaligned, or in the code's last word.
    EXPECT_TRUE(refused(make_elf(0x801A0240, nightfire_segments())));
    EXPECT_TRUE(refused(make_elf(0x80216460, nightfire_segments())));
    EXPECT_TRUE(refused(make_elf(0x80007102, nightfire_segments())));
    EXPECT_TRUE(refused(make_elf(0x800072E0 + 0x198F2C - 4, nightfire_segments())));
    EXPECT_TRUE(riftwii::parse_gc_elf(head.data(), head.size(), kFileBytes, kFloor, kCeiling, program, why));
    GcElfProgram last_two;
    EXPECT_TRUE(riftwii::parse_gc_elf(make_elf(0x800072E0 + 0x198F2C - 8, nightfire_segments()).data(),
                                      riftwii::kGcElfHeadBytes, kFileBytes, kFloor, kCeiling, last_two, why));
}

void test_disc_offset() {
    GcElfProgram program;
    std::string why;
    const std::vector<std::uint8_t> head = make_elf(0x80007100, nightfire_segments());
    EXPECT_TRUE(riftwii::parse_gc_elf(head.data(), head.size(), kFileBytes, kFloor, kCeiling, program, why));
    const std::uint32_t file = 0x3F2A8000;  // where the disc has the file
    std::uint32_t offset = 0;
    EXPECT_TRUE(riftwii::gc_elf_disc_offset(program, file, 0x80007100, offset));
    EXPECT_EQ(offset, file + 0x180);
    EXPECT_TRUE(riftwii::gc_elf_disc_offset(program, file, 0x8003C1F4, offset));
    EXPECT_EQ(offset, file + 0x360 + (0x8003C1F4 - 0x800072E0));
    // The code's last word, and the one past it (the next segment's start
    // is 0x801A0220, with a gap of zeroes between).
    EXPECT_TRUE(riftwii::gc_elf_disc_offset(program, file, 0x800072E0 + 0x198F2C - 4, offset));
    EXPECT_TRUE(!riftwii::gc_elf_disc_offset(program, file, 0x800072E0 + 0x198F2C, offset));
    // A zeroed tail (.bss) is not on the disc.
    EXPECT_TRUE(!riftwii::gc_elf_disc_offset(program, file, 0x80216460, offset));
    // A word that starts in the file's last 2 bytes of a segment does not count.
    EXPECT_TRUE(!riftwii::gc_elf_disc_offset(program, file, 0x8030E000 + 0x13E8, offset));
    // Past 4 GiB on the disc: refused rather than wrapped.
    EXPECT_TRUE(!riftwii::gc_elf_disc_offset(program, 0xFFFFFF00u, 0x80007100, offset));
}

void test_changed_words() {
    std::vector<std::uint8_t> original(16, 0), patched(16, 0);
    put32(original, 4, 0x3C60CC00);
    put32(patched, 4, 0x3C60CD00);
    put32(patched, 12, 0x4E800020);
    std::vector<std::pair<std::uint32_t, std::uint32_t>> changed;
    riftwii::gc_changed_words(0x80100000, original, patched, changed);
    EXPECT_EQ(changed.size(), static_cast<std::size_t>(2));
    if (changed.size() == 2) {
        EXPECT_EQ(changed[0].first, 0x80100004u);
        EXPECT_EQ(changed[0].second, 0x3C60CD00u);
        EXPECT_EQ(changed[1].first, 0x8010000Cu);
        EXPECT_EQ(changed[1].second, 0x4E800020u);
    }
}

void test_sort_patches() {
    std::vector<GcDiscPatch> patches = {{0x3000, 1}, {0x1000, 2}, {0x2000, 3}, {0x1004, 4}};
    std::string why;
    EXPECT_TRUE(riftwii::gc_sort_disc_patches(patches, why));
    EXPECT_EQ(patches[0].offset, 0x1000u);
    EXPECT_EQ(patches[1].offset, 0x1004u);
    EXPECT_EQ(patches[3].offset, 0x3000u);
    std::vector<GcDiscPatch> overlapping = {{0x1000, 1}, {0x1002, 2}};
    EXPECT_TRUE(!riftwii::gc_sort_disc_patches(overlapping, why));
    std::vector<GcDiscPatch> twice = {{0x1000, 1}, {0x1000, 1}};
    EXPECT_TRUE(!riftwii::gc_sort_disc_patches(twice, why));
}

void test_entry_words() {
    std::uint32_t words[2] = {};
    std::string why;
    // Nightfire.elf's __start: lis r1,0x8032 / ori r1,r1,0x1100. The blob
    // at the top of MEM1.
    EXPECT_TRUE(riftwii::gc_program_entry_words(0x80007100, 0x3C208032, 0x60211100, 1, 0x817DA000, words, why));
    EXPECT_EQ(words[0], 0x38000001u);  // li r0,1
    EXPECT_EQ(words[1], 0x48000000u | ((0x817DA000u - 0x80007104u) & 0x03FFFFFCu));
    // Backwards (a program above the blob is not one RiftWii takes, but
    // the branch still has to come out right).
    EXPECT_TRUE(riftwii::gc_program_entry_words(0x80500000, 0x60000000, 0x60000000, 2, 0x80400000, words, why));
    EXPECT_EQ(words[1], 0x48000000u | ((0x80400000u - 0x80500004u) & 0x03FFFFFCu));
    // A relative branch among the two cannot run from the blob's slot.
    EXPECT_TRUE(!riftwii::gc_program_entry_words(0x80007100, 0x48000011, 0x60000000, 1, 0x817DA000, words, why));
    EXPECT_TRUE(!riftwii::gc_program_entry_words(0x80007100, 0x60000000, 0x4182FFF8, 1, 0x817DA000, words, why));
    // blr and bctr are not relative: they replay anywhere.
    EXPECT_TRUE(riftwii::gc_program_entry_words(0x80007100, 0x7C0802A6, 0x4E800020, 1, 0x817DA000, words, why));
    // Out of a branch's 32 MB reach, or an index li cannot hold.
    EXPECT_TRUE(!riftwii::gc_program_entry_words(0x80007100, 0x60000000, 0x60000000, 1, 0x93000000, words, why));
    EXPECT_TRUE(!riftwii::gc_program_entry_words(0x80007100, 0x60000000, 0x60000000, 0x8000, 0x817DA000, words, why));
}

// The blob's patching of a read (gc_programs.h), piece by piece as the
// read arrives.
void test_patch_reads() {
    const std::vector<gc_disc_patch> patches = {{0x1000, 0x11223344}, {0x1010, 0xAABBCCDD}, {0x2002, 0x01020304}};
    std::vector<std::uint8_t> disc(0x3000, 0xEE);
    // The whole range at once.
    std::vector<std::uint8_t> whole(disc);
    EXPECT_EQ(gc_programs_patch(patches.data(), 3, whole.data(), 0, 0x3000), 3u);
    EXPECT_EQ(get32(whole, 0x1000), 0x11223344u);
    EXPECT_EQ(get32(whole, 0x1010), 0xAABBCCDDu);
    EXPECT_EQ(get32(whole, 0x2002), 0x01020304u);
    EXPECT_EQ(whole[0x1004], 0xEE);
    // The same, in pieces whose edges cut the patched words: the result is
    // the same bytes.
    std::vector<std::uint8_t> pieces(disc);
    const std::uint32_t edges[] = {0, 0x1002, 0x1011, 0x2003, 0x2005, 0x3000};
    for (unsigned i = 0; i + 1 < sizeof(edges) / sizeof(edges[0]); ++i) {
        gc_programs_patch(patches.data(), 3, pieces.data() + edges[i], edges[i], edges[i + 1] - edges[i]);
    }
    EXPECT_TRUE(pieces == whole);
    // A read with no patch in it, one right after a patch, none at all.
    std::vector<std::uint8_t> clean(0x100, 0xEE);
    EXPECT_EQ(gc_programs_patch(patches.data(), 3, clean.data(), 0x1100, 0x100), 0u);
    EXPECT_EQ(gc_programs_patch(patches.data(), 3, clean.data(), 0x1004, 0xC), 0u);
    EXPECT_EQ(gc_programs_patch(patches.data(), 0, clean.data(), 0x1000, 0x100), 0u);
    EXPECT_TRUE(clean == std::vector<std::uint8_t>(0x100, 0xEE));
    // A one-byte read in a patch's middle takes that byte alone.
    std::uint8_t one = 0xEE;
    EXPECT_EQ(gc_programs_patch(patches.data(), 3, &one, 0x1012, 1), 1u);
    EXPECT_EQ(one, 0xCC);
    // Many patches: the search finds the first one in the read.
    std::vector<gc_disc_patch> many;
    for (std::uint32_t i = 0; i < 1000; ++i) many.push_back({0x40000 + 8 * i, 0x60000000 | i});
    std::vector<std::uint8_t> block(16, 0);
    EXPECT_EQ(gc_programs_patch(many.data(), 1000, block.data(), 0x40000 + 8 * 600, 16), 2u);
    EXPECT_EQ(get32(block, 0), 0x60000000u | 600);
    EXPECT_EQ(get32(block, 8), 0x60000000u | 601);
    // At the top of the 32-bit range nothing wraps.
    const gc_disc_patch top = {0xFFFFFFF8u, 0x12345678};
    std::uint8_t tail[8] = {};
    EXPECT_EQ(gc_programs_patch(&top, 1, tail, 0xFFFFFFF8u, 8), 1u);
    EXPECT_EQ(tail[0], 0x12);
    EXPECT_EQ(gc_programs_patch(&top, 1, tail, 0, 8), 0u);
}

void test_resident_words() {
    // Two regions of RiftWii's part (the blob's start, then the ARAM stubs).
    auto region = [](std::uint32_t address, std::vector<std::uint32_t> words) {
        GcResidentRegion r;
        r.address = address;
        r.bytes.resize(words.size() * 4);
        for (std::size_t i = 0; i < words.size(); ++i) put32(r.bytes, i * 4, words[i]);
        return r;
    };
    const std::vector<GcResidentRegion> base = {region(0x817D0000, {0, 0, 0, 0}), region(0x817F0000, {0, 0})};
    // The main program's patches, and another program's.
    const std::vector<GcResidentRegion> main_program = {region(0x817D0000, {7, 0, 1, 0}), region(0x817F0000, {0, 0})};
    const std::vector<GcResidentRegion> other = {region(0x817D0000, {9, 0, 1, 5}), region(0x817F0000, {0, 3})};
    const std::vector<std::uint32_t> words = riftwii::gc_resident_words(base, {main_program, other}, {0x817D0008});
    // 0x817D0008 changed (1 in both) but is excluded; 0x817D0004 never changed.
    EXPECT_EQ(words.size(), static_cast<std::size_t>(3));
    if (words.size() == 3) {
        EXPECT_EQ(words[0], 0x817D0000u);
        EXPECT_EQ(words[1], 0x817D000Cu);
        EXPECT_EQ(words[2], 0x817F0004u);
    }
    EXPECT_EQ(riftwii::gc_resident_word(other, 0x817D0000), 9u);
    EXPECT_EQ(riftwii::gc_resident_word(main_program, 0x817D000C), 0u);
    EXPECT_EQ(riftwii::gc_resident_word(other, 0x817F0004), 3u);
    EXPECT_EQ(riftwii::gc_resident_word(other, 0x817F0008), 0u);  // past the region
    // No program changed anything: nothing to set.
    EXPECT_TRUE(riftwii::gc_resident_words(base, {base}, {}).empty());
}

}  // namespace

int main() {
    test_parse();
    test_disc_offset();
    test_changed_words();
    test_sort_patches();
    test_entry_words();
    test_patch_reads();
    test_resident_words();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "gcprograms tests passed" << std::endl;
    return 0;
}
