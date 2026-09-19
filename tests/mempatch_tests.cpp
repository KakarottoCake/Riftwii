// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/mempatch.hpp"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

// MEM1 stand-in: 64 KiB at 0x80000000, plus 4 KiB of "MEM2" at 0x90000000.
class FakeMemory final : public riftwii::MemoryAccess {
public:
    static constexpr std::uint32_t kMem1 = 0x80000000u;
    static constexpr std::uint32_t kMem1Size = 0x10000;
    static constexpr std::uint32_t kMem2 = 0x90000000u;
    static constexpr std::uint32_t kMem2Size = 0x1000;
    std::vector<std::uint8_t> mem1 = std::vector<std::uint8_t>(kMem1Size, 0);
    std::vector<std::uint8_t> mem2 = std::vector<std::uint8_t>(kMem2Size, 0);
    unsigned reads = 0;
    unsigned writes = 0;

    std::uint8_t* at(std::uint32_t address, std::size_t length) {
        if (address >= kMem1 && address - kMem1 + length <= kMem1Size) return mem1.data() + (address - kMem1);
        if (address >= kMem2 && address - kMem2 + length <= kMem2Size) return mem2.data() + (address - kMem2);
        return nullptr;
    }
    bool read(std::uint32_t address, std::uint8_t* out, std::size_t length) override {
        ++reads;
        const std::uint8_t* p = at(address, length);
        if (!p) return false;
        std::memcpy(out, p, length);
        return true;
    }
    bool write(std::uint32_t address, const std::uint8_t* bytes, std::size_t length) override {
        ++writes;
        std::uint8_t* p = at(address, length);
        if (!p) return false;
        std::memcpy(p, bytes, length);
        return true;
    }
    std::uint32_t word(std::uint32_t address) {
        const std::uint8_t* p = at(address, 4);
        return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
    }
    void put_word(std::uint32_t address, std::uint32_t v) {
        std::uint8_t* p = at(address, 4);
        p[0] = std::uint8_t(v >> 24); p[1] = std::uint8_t(v >> 16); p[2] = std::uint8_t(v >> 8); p[3] = std::uint8_t(v);
    }
    void put(std::uint32_t address, const std::string& s) { std::memcpy(at(address, s.size()), s.data(), s.size()); }
    std::string str(std::uint32_t address, std::size_t n) { return std::string(reinterpret_cast<char*>(at(address, n)), n); }
};

static std::vector<std::uint8_t> Bytes(const std::string& s) { return std::vector<std::uint8_t>(s.begin(), s.end()); }

static riftwii::MemoryPatch Plain(std::uint32_t offset, const std::string& value, const std::string& original = "") {
    riftwii::MemoryPatch p;
    p.offset = offset;
    p.has_offset = true;
    p.value = Bytes(value);
    p.original = Bytes(original);
    return p;
}

static const std::vector<riftwii::MemoryRegion> kWritable = {{0x80000000u, 0x10000}, {0x90000000u, 0x1000}};

static void test_plain() {
    FakeMemory mem;
    mem.put(0x80001000, "Kernel built");
    mem.put(0x80001100, "Console Type");
    std::vector<riftwii::MemoryPatch> patches;
    patches.push_back(Plain(0x1000, "Kernel BUILT", "Kernel built"));       // offset without the 0x80 prefix
    patches.push_back(Plain(0x80001100, "Riftwii Type", "Console Typo"));  // original differs: skipped
    patches.push_back(Plain(0x80001000, "kernel", "Kernel built"));        // no longer matches after the first
    patches.push_back(Plain(0x90000010, "\x01\x02\x03\x04"));
    std::vector<std::string> notes;
    std::string err;
    EXPECT_TRUE(riftwii::apply_memory_patches(patches, {}, kWritable, mem, notes, err));
    EXPECT_EQ(mem.str(0x80001000, 12), std::string("Kernel BUILT"));
    EXPECT_EQ(mem.str(0x80001100, 12), std::string("Console Type"));
    EXPECT_EQ(mem.word(0x90000010), std::uint32_t(0x01020304));
    EXPECT_EQ(notes.size(), std::size_t(4));
    if (notes.size() == 4) {
        EXPECT_EQ(notes[0], std::string("memory 0x80001000: 12 bytes written"));
        EXPECT_EQ(notes[1], std::string("memory 0x80001100: skipped, original differs"));
        EXPECT_EQ(notes[2], std::string("memory 0x80001000: skipped, original differs"));
        EXPECT_EQ(notes[3], std::string("memory 0x90000010: 4 bytes written"));
    }

    // Outside the writable ranges, no offset, no value: errors, and the
    // earlier patches in the list still apply.
    patches = {Plain(0x8000FFFE, "abcd")};
    EXPECT_FALSE(riftwii::apply_memory_patches(patches, {}, kWritable, mem, notes, err));
    patches = {Plain(0x80002000, "x"), Plain(0x80003000, "")};
    EXPECT_FALSE(riftwii::apply_memory_patches(patches, {}, kWritable, mem, notes, err));
    EXPECT_EQ(mem.str(0x80002000, 1), std::string("x"));
    patches = {Plain(0x80002000, "x")};
    patches[0].has_offset = false;
    EXPECT_FALSE(riftwii::apply_memory_patches(patches, {}, kWritable, mem, notes, err));
    patches = {Plain(0x80002000, "x")};
    patches[0].valuefile = "/a/b.bin";
    patches[0].value.clear();
    EXPECT_FALSE(riftwii::apply_memory_patches(patches, {}, kWritable, mem, notes, err));
    patches = {Plain(0x8000FFFC, "abcd", "abcdef")};  // the original check reads past the end
    EXPECT_FALSE(riftwii::apply_memory_patches(patches, {}, kWritable, mem, notes, err));
}

static void test_search() {
    FakeMemory mem;
    // Two loaded regions; the pattern appears in the second region first
    // in memory order but the first region is searched first.
    mem.put(0x80003001, "Firmware    ");   // region B, unaligned
    mem.put(0x80008004, "Firmware    ");   // region A
    mem.put(0x80008104, "Firmware    ");   // region A, second occurrence
    const std::vector<riftwii::MemoryRegion> loaded = {{0x80008000u, 0x200}, {0x80003000u, 0x100}};
    riftwii::MemoryPatch p;
    p.search = true;
    p.original = Bytes("Firmware    ");
    p.value = Bytes("Firmware RW ");
    p.align = 4;
    std::vector<std::string> notes;
    std::string err;
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    EXPECT_EQ(mem.str(0x80008004, 12), std::string("Firmware RW "));
    EXPECT_EQ(mem.str(0x80008104, 12), std::string("Firmware    "));  // only the first match
    EXPECT_EQ(mem.str(0x80003001, 12), std::string("Firmware    "));
    if (!notes.empty()) EXPECT_EQ(notes.back(), std::string("memory search: 12 bytes written at 0x80008004"));

    // Again: the first region no longer matches, the unaligned one in B is
    // invisible at stride 4, so the second occurrence in A is next.
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    EXPECT_EQ(mem.str(0x80008104, 12), std::string("Firmware RW "));
    // Stride 1 finds the unaligned one.
    p.align = 1;
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    EXPECT_EQ(mem.str(0x80003001, 12), std::string("Firmware RW "));
    // Nothing left: a note, not an error.
    const std::size_t before = notes.size();
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    EXPECT_EQ(notes.size(), before + 1);
    if (notes.size() == before + 1) EXPECT_EQ(notes.back(), std::string("memory search: no match for 12 bytes"));

    // A pattern across the chunk boundary is still found (chunks overlap).
    FakeMemory big;
    big.put(0x80000000u + 0x7FFA, "boundary");
    p.original = Bytes("boundary");
    p.value = Bytes("BOUNDARY");
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, {{0x80000000u, 0x10000}}, kWritable, big, notes, err));
    EXPECT_EQ(big.str(0x80000000u + 0x7FFA, 8), std::string("BOUNDARY"));
    // And one whose start is exactly the last possible position.
    big.put(0x80000000u + 0x10000 - 3, "end");
    p.original = Bytes("end");
    p.value = Bytes("END");
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, {{0x80000000u, 0x10000}}, kWritable, big, notes, err));
    EXPECT_EQ(big.str(0x80000000u + 0x10000 - 3, 3), std::string("END"));

    // Nothing loaded: a note, not an error; a huge stride does not hang.
    p.original = Bytes("Firmware RW ");
    p.value = Bytes("Firmware ok ");
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, {}, kWritable, mem, notes, err));
    EXPECT_EQ(notes.back(), std::string("memory search: no match for 12 bytes"));
    p.align = 0xFFFFFFFFFFFFFFFFull;
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    p.align = 4;

    // Malformed: lengths differ, no original, no value, both modes.
    riftwii::MemoryPatch bad = p;
    bad.value = Bytes("short");
    EXPECT_FALSE(riftwii::apply_memory_patches({bad}, loaded, kWritable, mem, notes, err));
    bad = p;
    bad.original.clear();
    EXPECT_FALSE(riftwii::apply_memory_patches({bad}, loaded, kWritable, mem, notes, err));
    bad = p;
    bad.value.clear();
    EXPECT_FALSE(riftwii::apply_memory_patches({bad}, loaded, kWritable, mem, notes, err));
    bad = p;
    bad.ocarina = true;
    EXPECT_FALSE(riftwii::apply_memory_patches({bad}, loaded, kWritable, mem, notes, err));
}

static void test_ocarina() {
    FakeMemory mem;
    // A function: the pattern, two more instructions, then blr; a second
    // blr later must not be touched.
    mem.put_word(0x80004000, 0x7C0802A6);  // mflr r0 (the pattern)
    mem.put_word(0x80004004, 0x90010004);
    mem.put_word(0x80004008, 0x38600000);
    mem.put_word(0x8000400C, 0x4E800020);  // blr
    mem.put_word(0x80004010, 0x4E800020);  // another blr
    const std::vector<riftwii::MemoryRegion> loaded = {{0x80004000u, 0x100}};
    riftwii::MemoryPatch p;
    p.ocarina = true;
    p.has_offset = true;
    p.offset = 0x80005000;
    p.value = {0x7C, 0x08, 0x02, 0xA6};
    std::vector<std::string> notes;
    std::string err;
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    // b 0x80005000 from 0x8000400C: displacement 0xFF4.
    EXPECT_EQ(mem.word(0x8000400C), std::uint32_t(0x48000FF4));
    EXPECT_EQ(mem.word(0x80004010), std::uint32_t(0x4E800020));
    if (!notes.empty()) EXPECT_EQ(notes.back(), std::string("ocarina: match at 0x80004000, blr at 0x8000400c -> b 0x80005000"));

    // A pattern that ends in the blr names that blr (the scan starts at
    // the match, as Dolphin's does), and the pattern is sought at 4-byte
    // steps: the same bytes at an odd offset are not a match.
    mem.put_word(0x8000400C, 0x4E800020);
    riftwii::MemoryPatch ending = p;
    ending.value = {0x38, 0x60, 0x00, 0x00, 0x4E, 0x80, 0x00, 0x20};
    EXPECT_TRUE(riftwii::apply_memory_patches({ending}, loaded, kWritable, mem, notes, err));
    EXPECT_EQ(mem.word(0x8000400C), std::uint32_t(0x48000FF4));
    EXPECT_EQ(mem.word(0x80004010), std::uint32_t(0x4E800020));
    mem.put_word(0x8000400C, 0x4E800020);
    FakeMemory odd;
    odd.put(0x80004001, std::string("\x7C\x08\x02\xA6", 4));
    odd.put_word(0x80004008, 0x4E800020);
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, odd, notes, err));
    EXPECT_EQ(notes.back(), std::string("ocarina: no match for 4 bytes"));
    EXPECT_EQ(odd.word(0x80004008), std::uint32_t(0x4E800020));

    // Backward branch, with the prefix-less offset form.
    mem.put_word(0x8000400C, 0x4E800020);
    p.offset = 0x3000;
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    // 0x80003000 - 0x8000400C = -0x100C -> 0x03FFEFF4 in the field.
    EXPECT_EQ(mem.word(0x8000400C), std::uint32_t(0x4BFFEFF4));

    // Pattern found but no blr after it inside the region; pattern absent.
    mem.put_word(0x8000400C, 0x60000000);
    mem.put_word(0x80004010, 0x60000000);
    const std::size_t before = notes.size();
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, {{0x80004000u, 0x14}}, kWritable, mem, notes, err));
    EXPECT_EQ(notes.size(), before + 1);
    if (notes.size() == before + 1) EXPECT_EQ(notes.back(), std::string("ocarina: match at 0x80004000 but no blr after it"));
    p.value = {0x11, 0x22, 0x33, 0x44};
    EXPECT_TRUE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    EXPECT_EQ(notes.back(), std::string("ocarina: no match for 4 bytes"));

    // Out of branch range, unaligned target, missing offset.
    mem.put_word(0x8000400C, 0x4E800020);
    p.value = {0x7C, 0x08, 0x02, 0xA6};
    p.offset = 0x90000000;
    EXPECT_FALSE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    p.offset = 0x80005002;
    EXPECT_FALSE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    p.offset = 0x80005000;
    p.has_offset = false;
    EXPECT_FALSE(riftwii::apply_memory_patches({p}, loaded, kWritable, mem, notes, err));
    EXPECT_EQ(mem.word(0x8000400C), std::uint32_t(0x4E800020));
}

int main() {
    test_plain();
    test_search();
    test_ocarina();
    if (g_failures == 0) {
        std::cout << "ALL MEMPATCH TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
