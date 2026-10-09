// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// The d2x information block (riftwii/d2xversion.hpp) and which versions
// RiftWii runs games on.
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/d2xversion.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

namespace {

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

// A first content as the d2x installer patches it.
std::vector<std::uint8_t> block(std::uint32_t major, int base, const char* release, const char* name = "d2x") {
    std::vector<std::uint8_t> b(0x40, 0);
    put32(b, 0, 0x1ee7c105u);
    put32(b, 4, 1);
    put32(b, 8, major);
    put32(b, 0x0C, static_cast<std::uint32_t>(base));
    std::memcpy(&b[0x10], name, std::strlen(name));
    std::memcpy(&b[0x20], release, std::strlen(release));
    return b;
}

D2xInfo parse(const std::vector<std::uint8_t>& b) { return parse_d2x_info(b.data(), b.size()); }

void test_parse() {
    const D2xInfo info = parse(block(11, 56, "beta3"));
    EXPECT_TRUE(info.d2x);
    EXPECT_EQ(info.major, 11);
    EXPECT_EQ(info.base, 56);
    EXPECT_EQ(info.release, std::string("beta3"));
    EXPECT_EQ(d2x_name(info), std::string("d2x v11 beta3"));

    // An IOS's own first content ("firmware.64.1204...") and short reads.
    std::vector<std::uint8_t> firm(0x40, 0);
    std::memcpy(firm.data(), "firmware.64.1204101033", 22);
    EXPECT_FALSE(parse(firm).d2x);
    EXPECT_FALSE(parse_d2x_info(block(11, 56, "beta3").data(), 0x2F).d2x);
    EXPECT_FALSE(parse_d2x_info(nullptr, 0x40).d2x);
    // The magic with another name: not d2x.
    EXPECT_FALSE(parse(block(11, 56, "beta3", "cios")).d2x);
    // The version in a wrong magic version.
    std::vector<std::uint8_t> other = block(11, 56, "beta3");
    put32(other, 4, 2);
    EXPECT_FALSE(parse(other).d2x);
}

void test_current() {
    EXPECT_TRUE(d2x_current(parse(block(11, 56, "beta3"))));
    EXPECT_TRUE(d2x_current(parse(block(11, 58, "beta4"))));
    EXPECT_TRUE(d2x_current(parse(block(11, 57, "beta12"))));
    EXPECT_TRUE(d2x_current(parse(block(11, 57, "final"))));
    EXPECT_TRUE(d2x_current(parse(block(11, 57, ""))));
    EXPECT_TRUE(d2x_current(parse(block(12, 57, "beta1"))));
    EXPECT_FALSE(d2x_current(parse(block(11, 56, "beta2"))));
    EXPECT_FALSE(d2x_current(parse(block(11, 56, "beta1"))));
    EXPECT_FALSE(d2x_current(parse(block(11, 56, "alpha5"))));
    EXPECT_FALSE(d2x_current(parse(block(11, 56, "beta"))));
    EXPECT_FALSE(d2x_current(parse(block(10, 56, "beta53-alt"))));
    EXPECT_FALSE(d2x_current(parse(block(8, 56, "final"))));
    EXPECT_FALSE(d2x_current(D2xInfo{}));
    EXPECT_EQ(d2x_name(parse(block(10, 56, "beta53-alt"))), std::string("d2x v10 beta53-alt"));
}

void test_allowed() {
    // v11's earlier betas start games (with a reminder); older d2x and
    // anything that is not d2x do not.
    EXPECT_TRUE(d2x_allowed(parse(block(11, 56, "beta1"))));
    EXPECT_TRUE(d2x_allowed(parse(block(11, 56, "beta2"))));
    EXPECT_TRUE(d2x_allowed(parse(block(11, 56, "beta3"))));
    EXPECT_TRUE(d2x_allowed(parse(block(12, 56, ""))));
    EXPECT_FALSE(d2x_allowed(parse(block(10, 56, "beta53-alt"))));
    EXPECT_FALSE(d2x_allowed(parse(block(11, 56, "beta3", "cios"))));
    EXPECT_FALSE(d2x_allowed(D2xInfo{}));
}

}  // namespace

int main() {
    test_parse();
    test_current();
    test_allowed();
    if (g_failures == 0) std::cout << "d2xversion: all passed" << std::endl;
    return g_failures == 0 ? 0 : 1;
}
