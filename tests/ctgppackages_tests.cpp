// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// /PACKAGES.BIN of CTGP-R's package disk (riftwii/ctgppackages.hpp).
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgppackages.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

// A made-up list in PACKAGES.BIN's format: five packages, invented versions.
static std::vector<std::uint8_t> made_list() {
    struct Entry { const char* name; std::uint32_t version, flags; };
    const Entry entries[] = {{"CHAN", 0x00020001, 0x0a000000}, {"CTGPR", 0x01020003, 0x0a000000},
                             {"CORE", 0x01020004, 0x0a000000}, {"EXTRA", 0x00010000, 0x08000000},
                             {"MYSTUFF", 0x01020005, 0x0a000000}};
    const std::uint32_t count = sizeof entries / sizeof entries[0];
    std::vector<std::uint8_t> b;
    auto put = [&](std::uint32_t v) {
        for (int i = 3; i >= 0; --i) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    };
    b.insert(b.end(), {'P', 'A', 'K', '0'});
    put((count + 1) * 16);
    put(0);
    put(count);
    for (const Entry& e : entries) {
        char name[8] = {};
        std::memcpy(name, e.name, std::strlen(e.name));
        b.insert(b.end(), name, name + 8);
        put(e.version);
        put(e.flags);
    }
    return b;
}

static std::uint32_t word_at(const std::vector<CtgpWordWrite>& w, std::uint32_t a) {
    for (const auto& x : w) if (x.address == a) return x.value;
    g_failures++;
    return 0xDEADBEEF;
}

static void test_list() {
    std::vector<CtgpPackage> p;
    std::string error;
    const std::vector<std::uint8_t> list = made_list();
    EXPECT_TRUE(ctgp_parse_packages(list.data(), list.size(), p, error));
    EXPECT_EQ(p.size(), (std::size_t)5);
    EXPECT_EQ(p[0].name, std::string("CHAN"));
    EXPECT_EQ(p[4].name, std::string("MYSTUFF"));
    EXPECT_EQ(ctgp_package_version(p, "CTGPR"), 0x01020003u);
    EXPECT_EQ(ctgp_package_version(p, "CORE"), 0x01020004u);
    EXPECT_EQ(ctgp_package_version(p, "MYSTUFF"), 0x01020005u);
    EXPECT_EQ(ctgp_package_version(p, "NOPE"), 0u);
    EXPECT_EQ(ctgp_package_version(p, "EXTRA"), 0x00010000u);
    const auto w = ctgp_package_id_writes(p);
    EXPECT_EQ(word_at(w, 0x816ff9d8), 0x01020004u);   // CORE
    EXPECT_EQ(word_at(w, 0x816ff9dc), 0x01020003u);   // CTGPR
    EXPECT_EQ(word_at(w, 0x816ff9e0), 0x00020001u);   // CHAN
    EXPECT_EQ(word_at(w, 0x816ff9e4), 0x01020005u);   // MYSTUFF
    EXPECT_EQ(word_at(w, 0x816ff9e8), 0u);
}

static void test_refusals() {
    std::vector<CtgpPackage> p;
    std::string error;
    const std::vector<std::uint8_t> b = made_list();
    auto bad = b;
    bad[0] = 'X';
    EXPECT_TRUE(!ctgp_parse_packages(bad.data(), bad.size(), p, error));
    bad = b;
    bad[7] = 0x50;                                    // size field no longer (count + 1) * 16
    EXPECT_TRUE(!ctgp_parse_packages(bad.data(), bad.size(), p, error));
    bad = b;
    bad[10] = 1;                                      // third word must be 0
    EXPECT_TRUE(!ctgp_parse_packages(bad.data(), bad.size(), p, error));
    EXPECT_TRUE(!ctgp_parse_packages(b.data(), 80, p, error));   // truncated
    EXPECT_TRUE(!ctgp_parse_packages(b.data(), 8, p, error));
}

int main() {
    test_list();
    test_refusals();
    if (g_failures == 0) std::cout << "ctgppackages tests passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
