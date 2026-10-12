// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <string>

#include "riftwii/extrasbudget.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) do { if ((a) != (b)) { std::cerr << "FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;

static void test_sums() {
    ExtrasBudget b;
    EXPECT_EQ(b.total(), 0u);
    EXPECT_EQ(b.list(), std::string(""));
    EXPECT_FALSE(b.heavy());
    b.add("crash recorder", 2816, 4864);
    b.add("nothing", 0, 0);  // left out
    b.add("mods", 96960, 353024);
    EXPECT_EQ(b.pieces.size(), 2u);
    EXPECT_EQ(b.mem1(), 2816u + 96960u);
    EXPECT_EQ(b.mem2(), 4864u + 353024u);
    // The biggest first, whole KB rounded up.
    EXPECT_EQ(b.list(), std::string("mods 440 KB, crash recorder 8 KB"));
    EXPECT_FALSE(b.heavy());
}

static void test_heavy() {
    ExtrasBudget b;
    b.add("mods", 96960, kExtrasHeavyMem2);
    EXPECT_FALSE(b.heavy());  // at the line, not past it
    b.add("screenshots", 0, 1);
    EXPECT_TRUE(b.heavy());
    ExtrasBudget c;
    c.add("mods", kExtrasHeavyMem1 + 1, 0);
    EXPECT_TRUE(c.heavy());
    EXPECT_EQ(whole_kb(0), 0u);
    EXPECT_EQ(whole_kb(1), 1u);
    EXPECT_EQ(whole_kb(1024), 1u);
    EXPECT_EQ(whole_kb(1025), 2u);
}

int main() {
    test_sums();
    test_heavy();
    if (g_failures == 0) {
        std::cout << "ALL EXTRASBUDGET TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
