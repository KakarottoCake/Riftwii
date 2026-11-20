// SPDX-FileCopyrightText: 2026 RiftWii contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// A folder's names kept to answer "is this file there?" without asking
// the card (riftwii/foldernames.hpp).
#include <iostream>
#include <string>

#include "riftwii/foldernames.hpp"

static int g_failures = 0;
#define EXPECT_TRUE(cond) do { if (!(cond)) { std::cerr << "FAILED: " #cond " at line " << __LINE__ << std::endl; g_failures++; } } while (0)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(a, b) EXPECT_TRUE((a) == (b))

// The covers' folder as Home asks about it: a stored cover, a miss, the
// marks beside them, and names that only differ in what follows the ID.
static void TestCoverNames() {
    riftwii::FolderNames names;
    for (const char* name : {"RMCE01.rwc", "RMCE01.rwc-second", "SMNE01.none", "R8PE01.second", "RSBE01.png",
                             "RMCE01@retro-rewind.mod"}) {
        EXPECT_TRUE(names.add(name));
    }
    // Asked before finish(): the same answers.
    EXPECT_TRUE(names.has("RMCE01.rwc"));
    EXPECT_FALSE(names.has("RMCE01.none"));
    names.finish();
    EXPECT_EQ(names.size(), std::size_t(6));
    EXPECT_TRUE(names.has("RMCE01.rwc"));
    EXPECT_TRUE(names.has("RMCE01.rwc-second"));
    EXPECT_TRUE(names.has("SMNE01.none"));
    EXPECT_TRUE(names.has("R8PE01.second"));
    EXPECT_TRUE(names.has("RSBE01.png"));
    EXPECT_TRUE(names.has("RMCE01@retro-rewind.mod"));
    EXPECT_FALSE(names.has("RMCE01.none"));
    EXPECT_FALSE(names.has("RMCE01.second"));
    EXPECT_FALSE(names.has("SMNE01.rwc"));
    EXPECT_FALSE(names.has("RMCE01"));
    EXPECT_FALSE(names.has("RMCE01.rw"));
    EXPECT_FALSE(names.has(""));
}

// FAT finds a file whatever the case of its name: so does the listing.
static void TestCaseIsIgnored() {
    riftwii::FolderNames names;
    EXPECT_TRUE(names.add("rmce01.RWC"));
    EXPECT_TRUE(names.add("Smne01.None"));
    names.finish();
    EXPECT_TRUE(names.has("RMCE01.rwc"));
    EXPECT_TRUE(names.has("SMNE01.none"));
    EXPECT_EQ(riftwii::folder_name_hash("RMCE01.rwc"), riftwii::folder_name_hash("rmce01.RWC"));
    // Only ASCII letters fold: other bytes are compared as they are.
    EXPECT_TRUE(riftwii::folder_name_hash("RMCE01.rwc") != riftwii::folder_name_hash("RMCE02.rwc"));
    EXPECT_TRUE(riftwii::folder_name_hash("[") != riftwii::folder_name_hash("{"));
    EXPECT_TRUE(riftwii::folder_name_hash("@") != riftwii::folder_name_hash("`"));
}

// A folder with more files than the listing keeps says so, and a cleared
// listing starts again.
static void TestTooManyNames() {
    riftwii::FolderNames names;
    bool all_added = true;
    for (std::size_t i = 0; i < riftwii::FolderNames::kMaxNames; ++i) {
        all_added = names.add("file" + std::to_string(i) + ".rwc") && all_added;
    }
    EXPECT_TRUE(all_added);
    EXPECT_FALSE(names.add("one-too-many.rwc"));
    EXPECT_EQ(names.size(), riftwii::FolderNames::kMaxNames);
    names.finish();
    EXPECT_TRUE(names.has("file0.rwc"));
    EXPECT_TRUE(names.has("FILE16383.RWC"));
    EXPECT_FALSE(names.has("file16384.rwc"));
    names.clear();
    EXPECT_EQ(names.size(), std::size_t(0));
    EXPECT_FALSE(names.has("file0.rwc"));
    EXPECT_TRUE(names.add("again.rwc"));
    EXPECT_TRUE(names.has("again.rwc"));
}

int main() {
    TestCoverNames();
    TestCaseIsIgnored();
    TestTooManyNames();
    if (g_failures == 0) {
        std::cout << "ALL FOLDER NAMES TESTS PASSED" << std::endl;
        return 0;
    }
    std::cerr << g_failures << " TEST CHECKS FAILED" << std::endl;
    return 1;
}
