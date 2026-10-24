// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// RiftWii's return stub for homebrew apps (riftwii/returnstub.hpp): the
// image filled with the titles to try, the two kinds of stub told apart,
// and DOLs that would land on the stub. The image here is made up in the
// header's shape; the real one is runtime/stub, built for the Wii only.
#include "riftwii/returnstub.hpp"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_be32(std::uint8_t* p, std::uint32_t v) {
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

std::uint32_t get_be32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

constexpr std::uint32_t kCodeBytes = 0x40;
constexpr std::uint64_t kChannel = 0x0001000155465457ull;  // 00010001-UFTW
constexpr std::uint64_t kWiiMenu = 0x0000000100000002ull;

// The stub as built: its branch, the two magics, the version and size,
// empty title slots, then code (0xAB bytes here, to see it kept).
Bytes stub_image() {
    Bytes image(RTSTUB_OFFSET_CODE + kCodeBytes, 0);
    put_be32(image.data(), 0x48000000u | RTSTUB_OFFSET_CODE);
    put_be32(image.data() + RTSTUB_OFFSET_HBC_MAGIC, RTSTUB_HBC_MAGIC_STUB);
    put_be32(image.data() + RTSTUB_OFFSET_HBC_MAGIC + 4, RTSTUB_HBC_MAGIC_HAXX);
    put_be32(image.data() + RTSTUB_OFFSET_MAGIC, RTSTUB_MAGIC);
    put_be32(image.data() + RTSTUB_OFFSET_VERSION, RTSTUB_VERSION);
    put_be32(image.data() + RTSTUB_OFFSET_SIZE, static_cast<std::uint32_t>(image.size()));
    for (std::uint32_t i = 0; i < kCodeBytes; ++i) image[RTSTUB_OFFSET_CODE + i] = 0xAB;
    return image;
}

riftwii::ReturnStubTitle title_with_view(std::uint64_t title, std::uint8_t fill) {
    riftwii::ReturnStubTitle t;
    t.title = title;
    t.view.fill(fill);
    return t;
}

void test_fill_two_titles() {
    const Bytes image = stub_image();
    std::string error;
    const Bytes filled = riftwii::fill_return_stub(image.data(), image.size(),
                                                   {title_with_view(kChannel, 0x11), title_with_view(kWiiMenu, 0x22)},
                                                   error);
    EXPECT_EQ(filled.size(), image.size());
    EXPECT_TRUE(error.empty());
    if (filled.size() != image.size()) return;
    EXPECT_EQ(get_be32(filled.data() + RTSTUB_OFFSET_COUNT), 2u);
    EXPECT_EQ(get_be32(filled.data() + RTSTUB_OFFSET_RESULT), 0u);
    const std::uint8_t* first = filled.data() + RTSTUB_OFFSET_TITLES;
    const std::uint8_t* second = first + RTSTUB_TITLE_BYTES;
    const std::uint8_t* third = second + RTSTUB_TITLE_BYTES;
    // The title ID big-endian at the slot's start, its view 0x20 on.
    EXPECT_EQ(get_be32(first), 0x00010001u);
    EXPECT_EQ(get_be32(first + 4), 0x55465457u);
    EXPECT_EQ(first[RTSTUB_TITLE_VIEW], 0x11);
    EXPECT_EQ(first[RTSTUB_TITLE_VIEW + RTSTUB_VIEW_BYTES - 1], 0x11);
    EXPECT_EQ(first[RTSTUB_TITLE_VIEW + RTSTUB_VIEW_BYTES], 0);
    EXPECT_EQ(get_be32(second), 0x00000001u);
    EXPECT_EQ(get_be32(second + 4), 0x00000002u);
    EXPECT_EQ(second[RTSTUB_TITLE_VIEW], 0x22);
    // The unused slot stays empty, and the code is left as it was.
    for (std::uint32_t i = 0; i < RTSTUB_TITLE_BYTES; ++i) EXPECT_EQ(third[i], 0);
    EXPECT_EQ(filled[RTSTUB_OFFSET_CODE], 0xAB);
    EXPECT_EQ(filled.back(), 0xAB);
    EXPECT_TRUE(riftwii::return_stub_kind(filled.data()) == riftwii::ReturnStubKind::RiftWii);
}

// A second fill of an image already filled replaces every slot.
void test_fill_again_clears_old_titles() {
    const Bytes image = stub_image();
    std::string error;
    const Bytes once = riftwii::fill_return_stub(
        image.data(), image.size(),
        {title_with_view(kChannel, 1), title_with_view(kChannel, 2), title_with_view(kWiiMenu, 3)}, error);
    const Bytes twice = riftwii::fill_return_stub(once.data(), once.size(), {title_with_view(kWiiMenu, 4)}, error);
    EXPECT_EQ(twice.size(), image.size());
    if (twice.size() != image.size()) return;
    EXPECT_EQ(get_be32(twice.data() + RTSTUB_OFFSET_COUNT), 1u);
    EXPECT_EQ(twice[RTSTUB_OFFSET_TITLES + RTSTUB_TITLE_BYTES], 0);
    EXPECT_EQ(twice[RTSTUB_OFFSET_TITLES + 2 * RTSTUB_TITLE_BYTES + RTSTUB_TITLE_VIEW], 0);
}

void test_fill_refuses() {
    const Bytes image = stub_image();
    std::string error;
    // No title, or more than the stub has slots for.
    EXPECT_TRUE(riftwii::fill_return_stub(image.data(), image.size(), {}, error).empty());
    EXPECT_TRUE(!error.empty());
    std::vector<riftwii::ReturnStubTitle> four(4, title_with_view(kChannel, 1));
    error.clear();
    EXPECT_TRUE(riftwii::fill_return_stub(image.data(), image.size(), four, error).empty());
    EXPECT_TRUE(!error.empty());
    const std::vector<riftwii::ReturnStubTitle> one = {title_with_view(kChannel, 1)};
    // The Homebrew Channel's magic alone: not RiftWii's stub.
    Bytes hbc = image;
    put_be32(hbc.data() + RTSTUB_OFFSET_MAGIC, 0);
    EXPECT_TRUE(riftwii::fill_return_stub(hbc.data(), hbc.size(), one, error).empty());
    // Another version of the layout.
    Bytes newer = image;
    put_be32(newer.data() + RTSTUB_OFFSET_VERSION, RTSTUB_VERSION + 1);
    EXPECT_TRUE(riftwii::fill_return_stub(newer.data(), newer.size(), one, error).empty());
    // Cut short: the header's size is more than the bytes given.
    EXPECT_TRUE(riftwii::fill_return_stub(image.data(), image.size() - 4, one, error).empty());
    // Too short to hold the header and the slots.
    EXPECT_TRUE(riftwii::fill_return_stub(image.data(), RTSTUB_OFFSET_CODE, one, error).empty());
    EXPECT_TRUE(riftwii::fill_return_stub(nullptr, image.size(), one, error).empty());
    // Bigger than the room under RTSTUB_LIMIT.
    Bytes huge(RTSTUB_LIMIT - RTSTUB_BASE + 32, 0);
    std::copy(image.begin(), image.begin() + RTSTUB_OFFSET_CODE, huge.begin());
    put_be32(huge.data() + RTSTUB_OFFSET_SIZE, static_cast<std::uint32_t>(huge.size()));
    EXPECT_TRUE(riftwii::fill_return_stub(huge.data(), huge.size(), one, error).empty());
}

void test_stub_kinds() {
    Bytes low(RTSTUB_OFFSET_VERSION, 0);
    EXPECT_TRUE(riftwii::return_stub_kind(low.data()) == riftwii::ReturnStubKind::None);
    EXPECT_TRUE(riftwii::return_stub_kind(nullptr) == riftwii::ReturnStubKind::None);
    put_be32(low.data() + RTSTUB_OFFSET_HBC_MAGIC, RTSTUB_HBC_MAGIC_STUB);
    EXPECT_TRUE(riftwii::return_stub_kind(low.data()) == riftwii::ReturnStubKind::None);  // "STUB" alone
    put_be32(low.data() + RTSTUB_OFFSET_HBC_MAGIC + 4, RTSTUB_HBC_MAGIC_HAXX);
    EXPECT_TRUE(riftwii::return_stub_kind(low.data()) == riftwii::ReturnStubKind::HomebrewChannel);
    put_be32(low.data() + RTSTUB_OFFSET_MAGIC, RTSTUB_MAGIC);
    EXPECT_TRUE(riftwii::return_stub_kind(low.data()) == riftwii::ReturnStubKind::RiftWii);
}

// A DOL with one text section and a BSS, as libogc programs are laid out.
Bytes dol_with(std::uint32_t text_address, std::uint32_t data_address, std::uint32_t bss_address,
               std::uint32_t bss_size) {
    Bytes dol(0x300, 0);
    put_be32(dol.data() + 0x00, 0x100);          // text 0: file offset
    put_be32(dol.data() + 0x48, text_address);   // its address
    put_be32(dol.data() + 0x90, 0x100);          // its size
    if (data_address != 0) {
        put_be32(dol.data() + 0x1C, 0x200);      // data 0
        put_be32(dol.data() + 0x64, data_address);
        put_be32(dol.data() + 0xAC, 0x100);
    }
    put_be32(dol.data() + 0xD8, bss_address);
    put_be32(dol.data() + 0xDC, bss_size);
    put_be32(dol.data() + 0xE0, text_address);  // the entry, in the text
    return dol;
}

void test_dol_clear_of_stub() {
    const Bytes usual = dol_with(0x80004000, 0x80100000, 0x80200000, 0x1000);
    EXPECT_TRUE(riftwii::dol_clear_of_return_stub(usual.data(), usual.size()));
    // Right below the stub, ending at its first byte: clear.
    const Bytes below = dol_with(0x80004000, RTSTUB_BASE - 0x100, 0x80200000, 0x1000);
    EXPECT_TRUE(riftwii::dol_clear_of_return_stub(below.data(), below.size()));
    // From the stub's limit on: clear.
    const Bytes above = dol_with(RTSTUB_LIMIT, 0, 0x80200000, 0x1000);
    EXPECT_TRUE(riftwii::dol_clear_of_return_stub(above.data(), above.size()));
    // A data section on the stub (a Gecko-style loader): not clear.
    const Bytes data_on = dol_with(0x80004000, 0x80001900, 0x80200000, 0x1000);
    EXPECT_TRUE(!riftwii::dol_clear_of_return_stub(data_on.data(), data_on.size()));
    // Text ending inside it.
    const Bytes text_on = dol_with(RTSTUB_BASE - 0x80, 0, 0x80200000, 0x1000);
    EXPECT_TRUE(!riftwii::dol_clear_of_return_stub(text_on.data(), text_on.size()));
    // A BSS that reaches up into it.
    const Bytes bss_on = dol_with(0x80004000, 0, 0x80001000, 0x2000);
    EXPECT_TRUE(!riftwii::dol_clear_of_return_stub(bss_on.data(), bss_on.size()));
    // Not a DOL at all.
    const Bytes junk(0x40, 0xFF);
    EXPECT_TRUE(!riftwii::dol_clear_of_return_stub(junk.data(), junk.size()));
    EXPECT_TRUE(!riftwii::dol_clear_of_return_stub(nullptr, 0));
}

}  // namespace

int main() {
    test_fill_two_titles();
    test_fill_again_clears_old_titles();
    test_fill_refuses();
    test_stub_kinds();
    test_dol_clear_of_stub();
    if (g_failures != 0) {
        std::cerr << g_failures << " failure(s)" << std::endl;
        return 1;
    }
    std::cout << "returnstub tests passed" << std::endl;
    return 0;
}
