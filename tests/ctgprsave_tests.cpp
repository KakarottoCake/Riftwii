// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// CTGP-R's SD save folder: names and which NAND files are copied (made-up names only).
#include <iostream>
#include <string>
#include <vector>

#include "riftwii/ctgprsave.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_EQ(a, b) do { if (!((a) == (b))) { std::cerr << "FAILED: " #a " == " #b " at line " << __LINE__ << std::endl; g_failures++; } } while (0)

using namespace riftwii;
using V = std::vector<std::string>;

int main() {
    EXPECT_EQ(ctgpr_save_region("RMCP01"), 'P');
    EXPECT_EQ(ctgpr_save_region("RMCE01"), 'E');
    EXPECT_EQ(ctgpr_save_region("RMCJ01"), 'J');
    EXPECT_EQ(ctgpr_save_region("RMCK01"), 'K');
    EXPECT_EQ(ctgpr_save_region("RMCX01"), 0);
    EXPECT_EQ(ctgpr_save_region("RSBE01"), 0);
    EXPECT_EQ(ctgpr_save_region("RMC"), 0);
    EXPECT_EQ(ctgpr_save_region(""), 0);
    EXPECT_EQ(ctgpr_sd_save_folder('P'), std::string("sd:/riivolution/save/RMCP"));
    EXPECT_EQ(ctgpr_nand_data_dir('P'), std::string("/title/00010004/524d4350/data"));
    EXPECT_EQ(ctgpr_nand_data_dir('E'), std::string("/title/00010004/524d4345/data"));
    EXPECT_EQ(ctgpr_nand_data_dir('J'), std::string("/title/00010004/524d434a/data"));
    EXPECT_EQ(ctgpr_nand_data_dir('K'), std::string("/title/00010004/524d434b/data"));
    EXPECT_EQ(ctgpr_save_folder_label("RMCK01"), std::string("RMCK"));
    EXPECT_EQ(ctgpr_save_folder_label("SMNE01"), std::string());

    // rksys.dat there (any case): skipped whole.
    EXPECT_TRUE(ctgpr_seed_skipped(V{"RKSYS.DAT"}));
    EXPECT_TRUE(ctgpr_seed_skipped(V{"a.bin", "rksys.dat"}));
    EXPECT_TRUE(!ctgpr_seed_skipped(V{}));
    EXPECT_TRUE(!ctgpr_seed_skipped(V{"a.bin"}));

    // Everything is copied, rksys.dat last.
    EXPECT_EQ(ctgpr_seed_plan(V{"rksys.dat", "a.bin", "b.vff"}, V{}), (V{"a.bin", "b.vff", "rksys.dat"}));
    // Nothing on the NAND (a new player): an empty plan, the folder alone is made.
    EXPECT_EQ(ctgpr_seed_plan(V{}, V{}), V{});
    // What the card holds is never copied over, whatever the case.
    EXPECT_EQ(ctgpr_seed_plan(V{"rksys.dat", "a.bin", "b.vff"}, V{"A.BIN"}), (V{"b.vff", "rksys.dat"}));
    // Odd names and the leftover of a stopped copy are left out.
    EXPECT_EQ(ctgpr_seed_plan(V{"..", "x/y", "z:q", "", "a.bin.rwtmp", "b.vff"}, V{}), (V{"b.vff"}));
    // Duplicates once.
    EXPECT_EQ(ctgpr_seed_plan(V{"a.bin", "A.BIN"}, V{}), V{"a.bin"});
    EXPECT_EQ(ctgpr_seed_temp_name("a.bin"), std::string("a.bin.rwtmp"));
    if (g_failures == 0) std::cout << "ctgprsave tests passed\n";
    return g_failures == 0 ? 0 : 1;
}
