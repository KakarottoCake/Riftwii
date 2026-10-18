// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "riftwii/stashimage.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) do { if (cond) { std::cerr << "FAILED: false expected for " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using Bytes = std::vector<std::uint8_t>;

static void Put32(Bytes& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

static void PutText(Bytes& b, std::size_t at, const std::string& text) {
    for (std::size_t i = 0; i < text.size(); ++i) b[at + i] = static_cast<std::uint8_t>(text[i]);
}

static std::string AsText(const std::uint8_t* data, std::uint32_t size) {
    return std::string(reinterpret_cast<const char*>(data), size);
}

// The layout tools/dol_stash.py writes, built by hand.
static Bytes MakeImage(const std::string& version, const std::vector<std::pair<std::string, std::string>>& items) {
    const std::size_t directory_end = riftwii::kStashHeaderBytes + riftwii::kStashEntryBytes * items.size();
    std::size_t offset = (directory_end + 31) & ~std::size_t(31);
    Bytes image(offset, 0);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::size_t entry = riftwii::kStashHeaderBytes + i * riftwii::kStashEntryBytes;
        PutText(image, entry, items[i].first);
        Put32(image, entry + riftwii::kStashNameBytes, static_cast<std::uint32_t>(offset));
        Put32(image, entry + riftwii::kStashNameBytes + 4, static_cast<std::uint32_t>(items[i].second.size()));
        image.resize((offset + items[i].second.size() + 31) & ~std::size_t(31), 0);
        PutText(image, offset, items[i].second);
        offset = image.size();
    }
    PutText(image, 0, "RWSTASH1");
    Put32(image, 8, static_cast<std::uint32_t>(image.size()));
    Put32(image, 16, static_cast<std::uint32_t>(items.size()));
    PutText(image, 20, version);
    Put32(image, 12, riftwii::stash_checksum(image.data() + 16, image.size() - 16));
    return image;
}

static void TestChecksum() {
    // FNV-1a's published values.
    EXPECT_EQ(riftwii::stash_checksum(nullptr, 0), 2166136261u);
    const std::uint8_t a[] = {'a'};
    EXPECT_EQ(riftwii::stash_checksum(a, 1), 0xE40C292Cu);
}

static void TestFind() {
    const Bytes image = MakeImage("2610-460", {{"menufont.zst", "font bytes"}, {"es.po.zst", "hola"}});
    EXPECT_EQ(riftwii::stash_check(image.data(), image.size(), "2610-460"), image.size());
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
    EXPECT_TRUE(riftwii::stash_find(image.data(), "es.po.zst", data, size));
    EXPECT_EQ(AsText(data, size), std::string("hola"));
    EXPECT_TRUE(riftwii::stash_find(image.data(), "menufont.zst", data, size));
    EXPECT_EQ(AsText(data, size), std::string("font bytes"));
    EXPECT_EQ((data - image.data()) % 32, 0);
    EXPECT_FALSE(riftwii::stash_find(image.data(), "es.po", data, size));  // whole names only
    EXPECT_FALSE(riftwii::stash_find(image.data(), "ko.po.zst", data, size));
}

static void TestRefusals() {
    const Bytes good = MakeImage("2610-460", {{"licence.zst", "GPL"}});
    // Another build's image: a warm restart must not use a stale copy.
    EXPECT_EQ(riftwii::stash_check(good.data(), good.size(), "2610-461"), 0u);
    EXPECT_EQ(riftwii::stash_check(good.data(), good.size(), "2610-46"), 0u);
    // Fewer bytes readable than the image claims.
    EXPECT_EQ(riftwii::stash_check(good.data(), good.size() - 1, "2610-460"), 0u);
    EXPECT_EQ(riftwii::stash_check(good.data(), 10, "2610-460"), 0u);
    EXPECT_EQ(riftwii::stash_check(nullptr, 0, "2610-460"), 0u);

    // One changed byte in the data: the checksum catches it, the shape does not.
    Bytes damaged = good;
    damaged[riftwii::kStashHeaderBytes + riftwii::kStashEntryBytes + 1] ^= 1;  // the "P" of "GPL"
    EXPECT_EQ(riftwii::stash_check(damaged.data(), damaged.size(), "2610-460"), 0u);
    EXPECT_EQ(riftwii::stash_check_shape(damaged.data(), damaged.size(), "2610-460"), damaged.size());

    // The magic crt0 erases once it has taken the copy.
    Bytes erased = good;
    erased[0] = 0;
    EXPECT_EQ(riftwii::stash_check_shape(erased.data(), erased.size(), "2610-460"), 0u);

    // An entry reaching past the image.
    Bytes past = good;
    Put32(past, riftwii::kStashHeaderBytes + riftwii::kStashNameBytes + 4, 0x10000);
    EXPECT_EQ(riftwii::stash_check_shape(past.data(), past.size(), "2610-460"), 0u);

    // A size past the 2 MiB bound, as heap bytes might read.
    Bytes huge = good;
    Put32(huge, 8, riftwii::kStashMaxBytes + 32);
    EXPECT_EQ(riftwii::stash_check_shape(huge.data(), huge.size(), "2610-460"), 0u);

    // More entries than the directory has room for.
    Bytes many = good;
    Put32(many, 16, 1000);
    EXPECT_EQ(riftwii::stash_check_shape(many.data(), many.size(), "2610-460"), 0u);
}

// tests/fixtures/stash_sample.bin was written by tools/dol_stash.py
// (version "test-1", greeting="hello stash", second.zst=...): the tool and
// this parser agree on the format.
static void TestToolFixture(const std::string& fixtures) {
    std::ifstream f(fixtures + "/stash_sample.bin", std::ios::binary);
    EXPECT_TRUE(f.good());
    const Bytes image((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    EXPECT_EQ(riftwii::stash_check(image.data(), image.size(), "test-1"), image.size());
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
    EXPECT_TRUE(riftwii::stash_find(image.data(), "greeting", data, size));
    EXPECT_EQ(AsText(data, size), std::string("hello stash"));
    EXPECT_TRUE(riftwii::stash_find(image.data(), "second.zst", data, size));
    EXPECT_EQ(AsText(data, size), std::string("second item, a little longer than the first"));
}

int main(int argc, char** argv) {
    TestChecksum();
    TestFind();
    TestRefusals();
    if (argc > 1) TestToolFixture(argv[1]);
    if (g_failures == 0) std::cout << "stash: all tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
